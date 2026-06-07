// SPDX-License-Identifier: Apache-2.0
//
// QUIC client to a Cloudflare edge using msquic.
//
// Memory ownership invariants (msquic is a C API with paired Open/Close calls;
// audit this section if you change anything):
//
//   * The QUIC_API_TABLE pointer is owned by detail::api_table_init/release
//     and is process-global. We bump a refcount on every QuicClientImpl ctor
//     and drop it in the dtor — when it hits 0 we MsQuicClose the table.
//
//   * Every HQUIC (registration, configuration, connection) we open is held
//     inside detail::UniqueHandle. On any failure path, scope unwind closes
//     them in reverse order — there is NO manual *Close in this file.
//
//   * Datagrams handed to DatagramSend require their backing buffer to stay
//     alive until SEND_COMPLETE. We allocate each as a std::unique_ptr<SendCtx>,
//     release() it across the C boundary, and reclaim it in the callback.
//
//   * Received-datagram buffers are owned by msquic and only valid during the
//     callback. We copy out before invoking the user handler.
//
//   * MsquicAsyncStream owns its HQUIC stream handle (via detail::UniqueHandle).
//     msquic holds a raw `this` pointer as the stream context, so the object
//     must never be copied or moved (KJ_DISALLOW_COPY_AND_MOVE).

#include "cfd/quic_client.hpp"
#include "cfd/frame.hpp"
#include "cfd/log.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#ifdef CFD_HAVE_MSQUIC
#  include "cfd/msquic_raii.hpp"
#endif

#if defined(CFD_HAVE_MSQUIC) && defined(CFD_HAVE_CAPNP)
#  include <capnp/rpc-twoparty.h>
#  include <kj/async.h>
#  include <kj/exception.h>
#  include "tunnelrpc.capnp.h"
#endif

namespace cfd::tunnel {

#ifdef CFD_HAVE_MSQUIC

namespace detail {
const QUIC_API_TABLE* g_msquic = nullptr;
static std::atomic<int> g_refcount{0};
static std::mutex       g_init_mu;

QUIC_STATUS api_table_init() noexcept {
    std::lock_guard<std::mutex> lk(g_init_mu);
    if (g_refcount.fetch_add(1) == 0) {
        const QUIC_STATUS s = MsQuicOpen2(&g_msquic);
        if (QUIC_FAILED(s)) {
            g_refcount.fetch_sub(1);
            return s;
        }
    }
    return QUIC_STATUS_SUCCESS;
}

void api_table_release() noexcept {
    std::lock_guard<std::mutex> lk(g_init_mu);
    if (g_refcount.fetch_sub(1) == 1) {
        MsQuicClose(g_msquic);
        g_msquic = nullptr;
    }
}
}  // namespace detail

namespace {

// Per-send context for QUIC datagrams owned across the msquic C boundary.
// We unique_ptr::release() into the DatagramSend call, then reclaim in the
// DATAGRAM_SEND_STATE_CHANGED callback.
struct SendCtx {
    std::vector<std::uint8_t> bytes;
    QUIC_BUFFER               qb{};
};

constexpr std::uint16_t kIdleTimeoutMs       = 30'000;
constexpr std::uint16_t kHandshakeTimeoutMs  = 10'000;
constexpr std::uint16_t kKeepAliveMs         = 5'000;

#ifdef CFD_HAVE_CAPNP

// ---------------------------------------------------------------------------
// MsquicAsyncStream — bridges a msquic bidi stream to kj::AsyncIoStream.
//
// Lifetime contract:
//   - The KJ event loop drives the RPC call synchronously via wait().
//   - msquic callbacks arrive on a msquic worker thread and use
//     CrossThreadPromiseFulfiller to wake the KJ loop.
//   - msquic holds a raw `this` pointer as the stream context, so this object
//     must not be moved or copied after StreamOpen.
// ---------------------------------------------------------------------------
class MsquicAsyncStream final : public kj::AsyncIoStream {
public:
    MsquicAsyncStream(const QUIC_API_TABLE* api, HQUIC connection) : api_(api) {
        HQUIC raw = nullptr;
        const QUIC_STATUS s = api_->StreamOpen(
            connection,
            QUIC_STREAM_OPEN_FLAG_NONE,
            &MsquicAsyncStream::s_callback,
            this,
            &raw);
        if (QUIC_FAILED(s))
            KJ_FAIL_REQUIRE("StreamOpen failed", s);
        stream_ = detail::wrap(raw, detail::HandleKind::Stream);

        if (QUIC_FAILED(api_->StreamStart(stream_.get(), QUIC_STREAM_START_FLAG_IMMEDIATE))) {
            stream_.reset();
            KJ_FAIL_REQUIRE("StreamStart failed");
        }
    }

    KJ_DISALLOW_COPY_AND_MOVE(MsquicAsyncStream);

    ~MsquicAsyncStream() noexcept override {
        // UniqueHandle dtor calls StreamClose, which drains all pending callbacks.
    }

    // tryRead: return bytes from recv_queue_ or park a wakeup fulfiller and retry.
    kj::Promise<size_t> tryRead(void* dst, size_t minBytes, size_t maxBytes) override {
        {
            std::unique_lock<std::mutex> lk(mu_);
            if (!recv_queue_.empty() || eof_) {
                const size_t n = drainLocked(dst, maxBytes);
                return kj::Promise<size_t>(n);
            }
        }

        // No data available — create a cross-thread fulfiller so the msquic
        // callback can wake us up.
        auto paf = kj::newPromiseAndCrossThreadFulfiller<void>();

        {
            std::unique_lock<std::mutex> lk(mu_);
            // Double-check: data may have arrived between the first check and
            // acquiring the lock again.
            if (!recv_queue_.empty() || eof_) {
                const size_t n = drainLocked(dst, maxBytes);
                return kj::Promise<size_t>(n);
            }
            read_wakeup_ = kj::mv(paf.fulfiller);
        }

        // When the fulfiller fires, recurse — data will be in recv_queue_.
        return paf.promise.then([this, dst, minBytes, maxBytes]() {
            return tryRead(dst, minBytes, maxBytes);
        });
    }

    // write: heap-allocate an RpcSendCtx, call StreamSend, fulfill on SEND_COMPLETE.
    kj::Promise<void> write(const void* src, size_t size) override {
        auto ctx = std::make_unique<RpcSendCtx>();
        ctx->buf.assign(
            static_cast<const std::uint8_t*>(src),
            static_cast<const std::uint8_t*>(src) + size);

        auto paf = kj::newPromiseAndCrossThreadFulfiller<void>();
        ctx->fulfiller = kj::mv(paf.fulfiller);

        QUIC_BUFFER qb{};
        qb.Buffer = ctx->buf.data();
        qb.Length = static_cast<std::uint32_t>(ctx->buf.size());

        // Release across the C boundary; reclaimed in SEND_COMPLETE.
        RpcSendCtx* raw = ctx.release();
        const QUIC_STATUS s = api_->StreamSend(
            stream_.get(), &qb, 1, QUIC_SEND_FLAG_NONE, raw);
        if (QUIC_FAILED(s)) {
            // Reclaim immediately on failure.
            std::unique_ptr<RpcSendCtx> reclaim(raw);
            KJ_FAIL_REQUIRE("StreamSend failed", s);
        }

        return kj::mv(paf.promise);
    }

    // write(pieces): flatten into a single allocation, delegate.
    kj::Promise<void> write(
        kj::ArrayPtr<const kj::ArrayPtr<const kj::byte>> pieces) override {
        size_t total = 0;
        for (auto& p : pieces) total += p.size();
        std::vector<std::uint8_t> flat;
        flat.reserve(total);
        for (auto& p : pieces)
            flat.insert(flat.end(), p.begin(), p.end());
        return write(flat.data(), flat.size());
    }

    void shutdownWrite() override {
        api_->StreamShutdown(stream_.get(), QUIC_STREAM_SHUTDOWN_FLAG_GRACEFUL, 0);
    }

    void abortRead() override {
        api_->StreamShutdown(
            stream_.get(),
            static_cast<QUIC_STREAM_SHUTDOWN_FLAGS>(QUIC_STREAM_SHUTDOWN_FLAG_ABORT_RECEIVE),
            QUIC_STATUS_ABORTED);
    }

    // whenWriteDisconnected: not tracked precisely — return a promise that
    // never resolves (acceptable per the kj::AsyncIoStream contract; callers
    // must not rely on this for correctness in our usage pattern).
    kj::Promise<void> whenWriteDisconnected() override {
        return kj::NEVER_DONE;
    }

    // Called from the watchdog thread when a timeout fires.
    void abort() noexcept {
        api_->StreamShutdown(
            stream_.get(),
            static_cast<QUIC_STREAM_SHUTDOWN_FLAGS>(
                QUIC_STREAM_SHUTDOWN_FLAG_ABORT |
                QUIC_STREAM_SHUTDOWN_FLAG_IMMEDIATE),
            QUIC_STATUS_ABORTED);
    }

private:
    // Per-RPC-write context: owns the send buffer and the cross-thread
    // fulfiller that fires when msquic signals SEND_COMPLETE.
    struct RpcSendCtx {
        std::vector<std::uint8_t>                      buf;
        kj::Own<kj::CrossThreadPromiseFulfiller<void>> fulfiller;
    };

    // Drain bytes from recv_queue_ into dst (up to maxBytes). Caller holds mu_.
    size_t drainLocked(void* dst, size_t maxBytes) {
        std::uint8_t* out = static_cast<std::uint8_t*>(dst);
        size_t copied = 0;
        while (copied < maxBytes && !recv_queue_.empty()) {
            Chunk& front = recv_queue_.front();
            const size_t avail  = front.data.size() - front.off;
            const size_t take   = std::min(avail, maxBytes - copied);
            std::memcpy(out + copied, front.data.data() + front.off, take);
            copied    += take;
            front.off += take;
            if (front.off == front.data.size())
                recv_queue_.pop_front();
        }
        return copied;
    }

    static QUIC_STATUS QUIC_API s_callback(
        HQUIC, void* ctx, QUIC_STREAM_EVENT* ev) noexcept {
        return static_cast<MsquicAsyncStream*>(ctx)->onEvent(*ev);
    }

    QUIC_STATUS onEvent(QUIC_STREAM_EVENT& ev) noexcept {
        switch (ev.Type) {
            case QUIC_STREAM_EVENT_RECEIVE: {
                kj::Own<kj::CrossThreadPromiseFulfiller<void>> wakeup;
                {
                    std::lock_guard<std::mutex> lk(mu_);
                    for (std::uint32_t i = 0; i < ev.RECEIVE.BufferCount; ++i) {
                        const auto& b = ev.RECEIVE.Buffers[i];
                        if (b.Length > 0) {
                            Chunk chunk;
                            chunk.data.assign(b.Buffer, b.Buffer + b.Length);
                            recv_queue_.push_back(std::move(chunk));
                        }
                    }
                    if (ev.RECEIVE.Flags & QUIC_RECEIVE_FLAG_FIN)
                        eof_ = true;
                    wakeup = kj::mv(read_wakeup_);
                }
                if (wakeup) wakeup->fulfill();
                break;
            }
            case QUIC_STREAM_EVENT_SEND_COMPLETE: {
                // Reclaim the RpcSendCtx and fire the write promise.
                auto* ctx = static_cast<RpcSendCtx*>(
                    ev.SEND_COMPLETE.ClientContext);
                if (ctx) {
                    std::unique_ptr<RpcSendCtx> owned(ctx);
                    if (owned->fulfiller) owned->fulfiller->fulfill();
                }
                break;
            }
            case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
            case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
            case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE: {
                kj::Own<kj::CrossThreadPromiseFulfiller<void>> wakeup;
                {
                    std::lock_guard<std::mutex> lk(mu_);
                    eof_ = true;
                    wakeup = kj::mv(read_wakeup_);
                }
                if (wakeup) wakeup->fulfill();
                break;
            }
            default: break;
        }
        return QUIC_STATUS_SUCCESS;
    }

    const QUIC_API_TABLE* api_;
    detail::UniqueHandle  stream_;

    std::mutex mu_;
    struct Chunk { std::vector<std::uint8_t> data; std::size_t off{0}; };
    std::deque<Chunk> recv_queue_;
    bool eof_{false};
    kj::Own<kj::CrossThreadPromiseFulfiller<void>> read_wakeup_;
};

#endif  // CFD_HAVE_CAPNP

}  // namespace

class QuicClientImpl {
public:
    explicit QuicClientImpl(QuicConfig cfg) : cfg_(std::move(cfg)) {}

    ~QuicClientImpl() {
        close();
        if (api_held_) detail::api_table_release();
    }

    std::error_code connect() {
        if (const auto s = detail::api_table_init(); QUIC_FAILED(s)) {
            LOG_ERROR("MsQuicOpen2 failed 0x%x", s);
            return std::make_error_code(std::errc::protocol_error);
        }
        api_held_ = true;

        // --- Registration ---
        QUIC_REGISTRATION_CONFIG rc{"cfd", QUIC_EXECUTION_PROFILE_LOW_LATENCY};
        HQUIC raw = nullptr;
        if (QUIC_FAILED(detail::g_msquic->RegistrationOpen(&rc, &raw)))
            return std::make_error_code(std::errc::protocol_error);
        registration_ = detail::wrap(raw, detail::HandleKind::Registration);

        // --- Configuration: ALPN + transport params + creds ---
        const QUIC_BUFFER alpn{
            static_cast<std::uint32_t>(cfg_.alpn.size()),
            const_cast<std::uint8_t*>(
                reinterpret_cast<const std::uint8_t*>(cfg_.alpn.data())),
        };

        QUIC_SETTINGS settings{};
        settings.IdleTimeoutMs                   = kIdleTimeoutMs;
        settings.IsSet.IdleTimeoutMs             = TRUE;
        settings.HandshakeIdleTimeoutMs          = kHandshakeTimeoutMs;
        settings.IsSet.HandshakeIdleTimeoutMs    = TRUE;
        settings.KeepAliveIntervalMs             = kKeepAliveMs;
        settings.IsSet.KeepAliveIntervalMs       = TRUE;
        settings.DatagramReceiveEnabled          = TRUE;
        settings.IsSet.DatagramReceiveEnabled    = TRUE;
        settings.PeerUnidiStreamCount            = 3;
        settings.IsSet.PeerUnidiStreamCount      = TRUE;

        raw = nullptr;
        if (QUIC_FAILED(detail::g_msquic->ConfigurationOpen(
                registration_.get(), &alpn, 1, &settings, sizeof(settings),
                nullptr, &raw)))
            return std::make_error_code(std::errc::protocol_error);
        configuration_ = detail::wrap(raw, detail::HandleKind::Configuration);

        // TLS policy:
        //   ca_bundle_path == "INSECURE"  -> explicit opt-in to skip validation
        //                                    (only for hand debugging; logged loud)
        //   ca_bundle_path empty          -> use the platform's system trust store
        //   otherwise                     -> use the file as the trust anchor
        //                                    (not yet wired through msquic creds —
        //                                     fall back to system store + a warn)
        QUIC_CREDENTIAL_CONFIG cred{};
        cred.Type  = QUIC_CREDENTIAL_TYPE_NONE;
        cred.Flags = QUIC_CREDENTIAL_FLAG_CLIENT;

        // CA file kept alive past the LoadCredential call via this local; msquic
        // copies into its own internal state before returning.
        QUIC_CERTIFICATE_FILE ca_file{};
        if (cfg_.ca_bundle_path == "INSECURE") {
            LOG_WARN("TLS validation DISABLED (ca_bundle_path=INSECURE). "
                     "Never use this against a real edge.");
            cred.Flags = static_cast<QUIC_CREDENTIAL_FLAGS>(
                cred.Flags | QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION);
        } else if (!cfg_.ca_bundle_path.empty()) {
#ifdef QUIC_CREDENTIAL_FLAG_SET_CA_CERTIFICATE_FILE
            // msquic >= 2.4 with OpenSSL backend honors this on top of any
            // credential type.
            ca_file.PrivateKeyFile  = nullptr;
            ca_file.CertificateFile = cfg_.ca_bundle_path.c_str();
            cred.CaCertificateFile  = cfg_.ca_bundle_path.c_str();
            cred.Flags = static_cast<QUIC_CREDENTIAL_FLAGS>(
                cred.Flags | QUIC_CREDENTIAL_FLAG_SET_CA_CERTIFICATE_FILE);
#else
            LOG_WARN("msquic build lacks SET_CA_CERTIFICATE_FILE; "
                     "falling back to system trust store");
            (void)ca_file;
#endif
        }
        if (QUIC_FAILED(detail::g_msquic->ConfigurationLoadCredential(
                configuration_.get(), &cred)))
            return std::make_error_code(std::errc::protocol_error);

        // --- Connection ---
        raw = nullptr;
        if (QUIC_FAILED(detail::g_msquic->ConnectionOpen(
                registration_.get(),
                &QuicClientImpl::s_conn_cb,
                this,
                &raw)))
            return std::make_error_code(std::errc::protocol_error);
        connection_ = detail::wrap(raw, detail::HandleKind::Connection);

        if (QUIC_FAILED(detail::g_msquic->ConnectionStart(
                connection_.get(),
                configuration_.get(),
                QUIC_ADDRESS_FAMILY_UNSPEC,
                cfg_.edge_host.c_str(),
                cfg_.edge_port))) {
            return std::make_error_code(std::errc::connection_refused);
        }

        // Block on the handshake outcome — Spike mode keeps this synchronous.
        std::unique_lock<std::mutex> lk(state_mu_);
        state_cv_.wait_for(lk, std::chrono::milliseconds(kHandshakeTimeoutMs + 1'000),
                           [this] { return handshake_done_; });
        if (!handshake_ok_) {
            LOG_ERROR("QUIC handshake failed/timeout to %s:%u",
                      cfg_.edge_host.c_str(), cfg_.edge_port);
            return std::make_error_code(std::errc::connection_aborted);
        }
        LOG_INFO("QUIC connected to %s:%u alpn=%s",
                 cfg_.edge_host.c_str(), cfg_.edge_port, cfg_.alpn.c_str());
        return {};
    }

    // ---- Control stream: tunnelrpc.RegisterConnection via capnp-rpc --------
    //
    // Opens a fresh bidi QUIC stream, wraps it in MsquicAsyncStream (which
    // implements kj::AsyncIoStream), then drives a capnp::TwoPartyClient on
    // top of it within a local KJ event loop.  Everything is synchronous from
    // the caller's perspective; the KJ loop pumps I/O via wait().
    std::error_code register_connection(const RegisterRequest& req,
                                        RegisterResponse& resp_out,
                                        int timeout_ms) {
        if (!connection_ || !handshake_ok_)
            return std::make_error_code(std::errc::not_connected);

#ifndef CFD_HAVE_CAPNP
        LOG_WARN("register_connection: built without capnp");
        return std::make_error_code(std::errc::not_supported);
#else
        kj::EventLoop loop;
        kj::WaitScope waitScope(loop);

        KJ_IF_MAYBE(err, kj::runCatchingExceptions([&]() {
            // stream owns the HQUIC; its dtor calls StreamClose synchronously.
            auto stream = kj::heap<MsquicAsyncStream>(
                detail::g_msquic, connection_.get());

            // Watchdog: abort the stream if the call takes too long.
            std::atomic<bool> done{false};
            auto watchdog = std::thread([&] {
                std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
                if (!done.load(std::memory_order_relaxed)) stream->abort();
            });
            // KJ_DEFER runs in LIFO order before stream is destroyed, so the
            // watchdog is always joined before the stream handle disappears.
            KJ_DEFER({ done.store(true, std::memory_order_relaxed); watchdog.join(); });

            capnp::TwoPartyClient rpc(*stream);
            auto cap = rpc.bootstrap().castAs<RegistrationServer>();

            auto req_msg = cap.registerConnectionRequest();

            // auth
            {
                auto auth = req_msg.initAuth();
                auth.setAccountTag(req.auth.account_tag);
                auth.setTunnelSecret(capnp::Data::Reader(
                    req.auth.tunnel_secret.data(),
                    req.auth.tunnel_secret.size()));
            }
            // tunnelId
            req_msg.setTunnelId(capnp::Data::Reader(
                req.tunnel_uuid.data(), req.tunnel_uuid.size()));
            req_msg.setConnIndex(req.conn_index);

            // options → client info
            {
                auto opts = req_msg.initOptions();
                auto ci   = opts.initClient();
                ci.setClientId(capnp::Data::Reader(
                    req.tunnel_uuid.data(), req.tunnel_uuid.size()));
                ci.setVersion(req.version);
                ci.setArch(req.arch);
                auto feats = ci.initFeatures(
                    static_cast<unsigned>(req.features.size()));
                for (unsigned i = 0;
                     i < static_cast<unsigned>(req.features.size()); ++i)
                    feats.set(i, req.features[i]);
            }

            // Drive the KJ event loop until we get a response.
            auto result = req_msg.send().wait(waitScope).getResult();

            if (result.isConnectionDetails()) {
                auto d   = result.getConnectionDetails();
                auto uid = d.getUuid();
                if (uid.size() == 16)
                    std::memcpy(resp_out.assigned_uuid.data(), uid.begin(), 16);
                resp_out.location_name = d.getLocationName().cStr();
                resp_out.ok = true;
            } else {
                auto e = result.getError();
                resp_out.error_cause         = e.getCause().cStr();
                resp_out.retry_after_seconds = e.getRetryAfter();
                resp_out.should_retry        = e.getShouldRetry();
                resp_out.ok                  = false;
            }
        })) {
            LOG_ERROR("register_connection rpc: %s", err->getDescription().cStr());
            return std::make_error_code(std::errc::protocol_error);
        }

        if (!resp_out.ok) {
            LOG_WARN("RegisterConnection rejected: %s (retry=%d after=%lld)",
                     resp_out.error_cause.c_str(), resp_out.should_retry,
                     static_cast<long long>(resp_out.retry_after_seconds));
            return std::make_error_code(std::errc::permission_denied);
        }
        LOG_INFO("RegisterConnection ok: location=%s", resp_out.location_name.c_str());
        return {};
#endif  // CFD_HAVE_CAPNP
    }

    void send_packet(const std::uint8_t* data, std::size_t len) noexcept {
        if (!connection_ || !handshake_ok_) return;
        try {
            auto ctx = std::make_unique<SendCtx>();
            Frame f;
            f.type    = FrameType::IpPacket;
            f.flow_id = 0;
            f.payload.assign(data, data + len);
            encode(f, ctx->bytes);
            ctx->qb.Buffer = ctx->bytes.data();
            ctx->qb.Length = static_cast<std::uint32_t>(ctx->bytes.size());

            // Release ownership to msquic; reclaimed in DATAGRAM_SEND_STATE_CHANGED.
            SendCtx* raw = ctx.release();
            const QUIC_STATUS s = detail::g_msquic->DatagramSend(
                connection_.get(), &raw->qb, 1, QUIC_SEND_FLAG_NONE, raw);
            if (QUIC_FAILED(s)) {
                std::unique_ptr<SendCtx> reclaim(raw);  // free on failure
                LOG_DEBUG("DatagramSend failed 0x%x", s);
            }
        } catch (const std::exception& e) {
            LOG_WARN("send_packet: %s", e.what());
        }
    }

    void set_handler(QuicClient::InboundHandler h) {
        std::lock_guard<std::mutex> lk(state_mu_);
        handler_ = std::move(h);
    }

    void close() noexcept {
        // Shut down gracefully if still connected; UniqueHandle dtors do the rest.
        if (connection_) {
            detail::g_msquic->ConnectionShutdown(
                connection_.get(), QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
        }
        connection_.reset();
        configuration_.reset();
        registration_.reset();
    }

private:
    // --- Connection callback (msquic worker thread) ---
    static QUIC_STATUS QUIC_API s_conn_cb(
        HQUIC, void* ctx, QUIC_CONNECTION_EVENT* ev) noexcept {
        auto* self = static_cast<QuicClientImpl*>(ctx);
        return self->on_conn_event(*ev);
    }

    QUIC_STATUS on_conn_event(QUIC_CONNECTION_EVENT& ev) noexcept {
        switch (ev.Type) {
            case QUIC_CONNECTION_EVENT_CONNECTED: {
                {
                    std::lock_guard<std::mutex> lk(state_mu_);
                    handshake_ok_ = true;
                    handshake_done_ = true;
                }
                state_cv_.notify_all();
                break;
            }
            case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
            case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
            case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE: {
                {
                    std::lock_guard<std::mutex> lk(state_mu_);
                    handshake_done_ = true;  // unblock connect() even on failure
                }
                state_cv_.notify_all();
                break;
            }
            case QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED: {
                deliver_inbound(ev.DATAGRAM_RECEIVED.Buffer->Buffer,
                                ev.DATAGRAM_RECEIVED.Buffer->Length);
                break;
            }
            case QUIC_CONNECTION_EVENT_DATAGRAM_SEND_STATE_CHANGED: {
                // ClientContext is the SendCtx* we released above. Reclaim it
                // on terminal states only — see msquic docs for which states
                // mean "buffer is no longer referenced".
                const auto state = ev.DATAGRAM_SEND_STATE_CHANGED.State;
                if (state == QUIC_DATAGRAM_SEND_SENT ||
                    state == QUIC_DATAGRAM_SEND_LOST_DISCARDED ||
                    state == QUIC_DATAGRAM_SEND_CANCELED ||
                    state == QUIC_DATAGRAM_SEND_ACKNOWLEDGED) {
                    std::unique_ptr<SendCtx> ctx(
                        static_cast<SendCtx*>(
                            ev.DATAGRAM_SEND_STATE_CHANGED.ClientContext));
                    (void)ctx;  // freed at end of scope
                }
                break;
            }
            default: break;
        }
        return QUIC_STATUS_SUCCESS;
    }

    void deliver_inbound(const std::uint8_t* data, std::uint32_t len) noexcept {
        DecodeResult dr{};
        if (!decode(std::span<const std::uint8_t>(data, len), dr)) return;
        if (dr.type != FrameType::IpPacket) return;

        QuicClient::InboundHandler h;
        {
            std::lock_guard<std::mutex> lk(state_mu_);
            h = handler_;
        }
        if (h) h(dr.payload.data(), dr.payload.size());
    }

    // Declaration order = construction order; reverse = destruction order.
    // Members the msquic callbacks may touch (state_*, handler_) must outlive
    // `connection_`. ConnectionClose drains callbacks synchronously, so once
    // `connection_` is destroyed the callbacks can no longer fire — making it
    // safe for the trailing members to die next.
    QuicConfig                  cfg_;
    bool                        api_held_{false};

    std::mutex                  state_mu_;
    std::condition_variable     state_cv_;
    bool                        handshake_done_{false};
    bool                        handshake_ok_{false};
    QuicClient::InboundHandler  handler_;

    detail::UniqueHandle        registration_;
    detail::UniqueHandle        configuration_;
    detail::UniqueHandle        connection_;
};

#else  // ---------- stub when built without msquic ----------

class QuicClientImpl {
public:
    explicit QuicClientImpl(QuicConfig cfg) : cfg_(std::move(cfg)) {}
    std::error_code connect() {
        LOG_WARN("compiled without msquic; QUIC connect is a no-op");
        return std::make_error_code(std::errc::not_supported);
    }
    void send_packet(const std::uint8_t*, std::size_t) noexcept {}
    void set_handler(QuicClient::InboundHandler) {}
    void close() noexcept {}
    std::error_code register_connection(const RegisterRequest&, RegisterResponse&, int) {
        return std::make_error_code(std::errc::not_supported);
    }
private:
    QuicConfig cfg_;
};

#endif

// --- Thin facade ---------------------------------------------------------

QuicClient::QuicClient(QuicConfig cfg)
    : impl_(std::make_unique<QuicClientImpl>(std::move(cfg))) {}

QuicClient::~QuicClient() = default;

std::error_code QuicClient::connect()                                       { return impl_->connect(); }
void QuicClient::on_packet(const std::uint8_t* d, std::size_t n) noexcept  { impl_->send_packet(d, n); }
void QuicClient::set_inbound_handler(InboundHandler h)                      { impl_->set_handler(std::move(h)); }
void QuicClient::close() noexcept                                           { impl_->close(); }
std::error_code QuicClient::register_connection(const RegisterRequest& r,
                                                RegisterResponse& out,
                                                int timeout_ms) {
    return impl_->register_connection(r, out, timeout_ms);
}

}  // namespace cfd::tunnel
