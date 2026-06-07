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

#include "cfd/quic_client.hpp"
#include "cfd/frame.hpp"
#include "cfd/log.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef CFD_HAVE_MSQUIC
#  include "cfd/msquic_raii.hpp"
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

// Per-send context owned across the msquic C boundary. We unique_ptr::release()
// into the QUIC_BUFFER.Buffer pointer's parent struct, then reclaim in the
// SEND_COMPLETE callback.
struct SendCtx {
    std::vector<std::uint8_t> bytes;
    QUIC_BUFFER               qb{};
};

// Per-control-stream state. The instance is owned by a shared_ptr in
// QuicClientImpl::live_streams_. msquic's callback receives a raw pointer
// (the map key); the shared_ptr keeps it alive until STREAM_SHUTDOWN_COMPLETE
// removes the map entry. This is the only place we have to be careful about
// the lifetime crossing the C boundary.
struct StreamCtx {
    void*                       parent{nullptr};   // QuicClientImpl* (forward-decl gymnastics)
    detail::UniqueHandle        handle;
    std::vector<std::uint8_t>   send_buf;
    QUIC_BUFFER                 send_qb{};
    std::vector<std::uint8_t>   recv_buf;
    std::mutex                  mu;
    std::condition_variable     cv;
    bool                        done{false};
    bool                        ok{false};
};

constexpr std::uint16_t kIdleTimeoutMs       = 30'000;
constexpr std::uint16_t kHandshakeTimeoutMs  = 10'000;
constexpr std::uint16_t kKeepAliveMs         = 5'000;

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

    // ---- Control stream: tunnelrpc.RegisterConnection ------------------
    //
    // Ownership note: the stream is held in a UniqueHandle scoped to this
    // method. On any failure path — send failure, peer abort, timeout — the
    // unique_ptr dtor runs StreamClose, releasing all msquic state.
    std::error_code register_connection(const RegisterRequest& req,
                                        RegisterResponse& resp_out,
                                        int timeout_ms) {
        if (!connection_ || !handshake_ok_)
            return std::make_error_code(std::errc::not_connected);

        auto wire = encode_register_request(req);
        if (wire.empty())
            return std::make_error_code(std::errc::not_supported);

        // Per-stream context. Lives on the heap because msquic invokes the
        // callback on a worker thread for an arbitrary duration; we hand a
        // shared_ptr to msquic via raw ptr and reclaim on STREAM_SHUTDOWN_COMPLETE.
        auto ctx = std::make_shared<StreamCtx>();
        ctx->parent = this;

        HQUIC raw = nullptr;
        if (QUIC_FAILED(detail::g_msquic->StreamOpen(
                connection_.get(),
                QUIC_STREAM_OPEN_FLAG_NONE,
                &QuicClientImpl::s_stream_cb,
                ctx.get(),
                &raw)))
            return std::make_error_code(std::errc::protocol_error);
        ctx->handle = detail::wrap(raw, detail::HandleKind::Stream);
        // Keep the shared_ptr alive while msquic might call back. We store it
        // in a member set keyed by the raw pointer used as the context.
        {
            std::lock_guard<std::mutex> lk(streams_mu_);
            live_streams_[ctx.get()] = ctx;
        }

        // If StreamStart/Send fail we will never get SHUTDOWN_COMPLETE, so we
        // must remove the live_streams_ entry ourselves on those paths.
        auto purge_on_fail = [&]() noexcept {
            std::lock_guard<std::mutex> lk(streams_mu_);
            live_streams_.erase(ctx.get());
        };

        if (QUIC_FAILED(detail::g_msquic->StreamStart(
                ctx->handle.get(), QUIC_STREAM_START_FLAG_IMMEDIATE))) {
            purge_on_fail();
            return std::make_error_code(std::errc::protocol_error);
        }

        // Send the request, FIN on the send side — server reads to EOF, replies.
        ctx->send_buf = std::move(wire);
        ctx->send_qb.Buffer = ctx->send_buf.data();
        ctx->send_qb.Length = static_cast<std::uint32_t>(ctx->send_buf.size());
        if (QUIC_FAILED(detail::g_msquic->StreamSend(
                ctx->handle.get(), &ctx->send_qb, 1,
                QUIC_SEND_FLAG_FIN, nullptr))) {
            purge_on_fail();
            return std::make_error_code(std::errc::io_error);
        }

        // Wait for response or timeout.
        std::unique_lock<std::mutex> lk(ctx->mu);
        const bool got = ctx->cv.wait_for(
            lk, std::chrono::milliseconds(timeout_ms),
            [&] { return ctx->done; });
        if (!got) {
            // Force STREAM_SHUTDOWN_COMPLETE so live_streams_ purges the entry.
            // Without this the StreamCtx leaks for the lifetime of the QUIC
            // connection. See docs/ISSUES.md ISSUE-002.
            lk.unlock();
            detail::g_msquic->StreamShutdown(
                ctx->handle.get(),
                static_cast<QUIC_STREAM_SHUTDOWN_FLAGS>(
                    QUIC_STREAM_SHUTDOWN_FLAG_ABORT |
                    QUIC_STREAM_SHUTDOWN_FLAG_IMMEDIATE),
                0);
            return std::make_error_code(std::errc::timed_out);
        }
        if (!ctx->ok)
            return std::make_error_code(std::errc::protocol_error);

        auto resp = decode_register_response(ctx->recv_buf.data(),
                                             ctx->recv_buf.size());
        if (!resp)
            return std::make_error_code(std::errc::bad_message);
        resp_out = *resp;
        if (!resp_out.ok) {
            LOG_WARN("RegisterConnection rejected: %s (retry=%d after=%lld)",
                     resp_out.error_cause.c_str(),
                     resp_out.should_retry,
                     static_cast<long long>(resp_out.retry_after_seconds));
            return std::make_error_code(std::errc::permission_denied);
        }
        LOG_INFO("RegisterConnection ok: location=%s", resp_out.location_name.c_str());
        return {};
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
    static QUIC_STATUS QUIC_API s_conn_cb(HQUIC, void* ctx, QUIC_CONNECTION_EVENT* ev) noexcept {
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

    // --- Stream callback (msquic worker thread) ---
    static QUIC_STATUS QUIC_API s_stream_cb(HQUIC, void* ctx, QUIC_STREAM_EVENT* ev) noexcept {
        auto* sc = static_cast<StreamCtx*>(ctx);
        return static_cast<QuicClientImpl*>(sc->parent)->on_stream_event(sc, *ev);
    }

    QUIC_STATUS on_stream_event(StreamCtx* sc, QUIC_STREAM_EVENT& ev) noexcept {
        switch (ev.Type) {
            case QUIC_STREAM_EVENT_RECEIVE: {
                std::lock_guard<std::mutex> lk(sc->mu);
                for (std::uint32_t i = 0; i < ev.RECEIVE.BufferCount; ++i) {
                    const auto& b = ev.RECEIVE.Buffers[i];
                    sc->recv_buf.insert(sc->recv_buf.end(),
                                        b.Buffer, b.Buffer + b.Length);
                }
                if (ev.RECEIVE.Flags & QUIC_RECEIVE_FLAG_FIN) {
                    sc->ok = true;
                    sc->done = true;
                    sc->cv.notify_all();
                }
                break;
            }
            case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
            case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED: {
                std::lock_guard<std::mutex> lk(sc->mu);
                sc->ok = false;
                sc->done = true;
                sc->cv.notify_all();
                break;
            }
            case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE: {
                {
                    std::lock_guard<std::mutex> lk(sc->mu);
                    if (!sc->done) { sc->done = true; sc->cv.notify_all(); }
                }
                // Drop the shared_ptr; sc may be deleted at the closing brace.
                std::lock_guard<std::mutex> lk(streams_mu_);
                live_streams_.erase(sc);
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
    // Members the msquic callbacks may touch (state_*, handler_, streams_*)
    // must outlive `connection_`. ConnectionClose drains callbacks
    // synchronously, so once `connection_` is destroyed the callbacks can no
    // longer fire — making it safe for the trailing members to die next.
    QuicConfig                  cfg_;
    bool                        api_held_{false};

    std::mutex                  state_mu_;
    std::condition_variable     state_cv_;
    bool                        handshake_done_{false};
    bool                        handshake_ok_{false};
    QuicClient::InboundHandler  handler_;

    std::mutex                                                  streams_mu_;
    std::unordered_map<StreamCtx*, std::shared_ptr<StreamCtx>>  live_streams_;

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
