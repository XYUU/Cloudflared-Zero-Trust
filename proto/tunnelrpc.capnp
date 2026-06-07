# SPDX-License-Identifier: Apache-2.0
#
# Subset of cloudflared's tunnelrpc schema needed to register a connection.
# Source of truth: github.com/cloudflare/cloudflared/tunnelrpc/proto/*.capnp
#
# We keep only the messages we *send* (RegisterConnection / Unregister) and
# *receive* (the result wrapper), so changes upstream to unrelated messages
# do not force us to re-vendor.

@0xc082ef6e0d42ed1d;

using Go = import "/go.capnp";   # cloudflared uses Go annotations; harmless here

struct ConnectionOptions {
    client            @0 :ClientInfo;
    originLocalIp     @1 :Data;
    replaceExisting   @2 :Bool;
    compressionQuality @3 :UInt8;
    numPreviousAttempts @4 :UInt8;
}

struct ClientInfo {
    clientId  @0 :Data;     # 16-byte UUID
    features  @1 :List(Text);
    version   @2 :Text;     # e.g. "cfd-cpp/0.1.0"
    arch      @3 :Text;     # e.g. "linux_mipsel"
}

struct ConnectionDetails {
    uuid           @0 :Data;
    locationName   @1 :Text;
    tunnelIsRemotelyConfigured @2 :Bool;
}

struct ConnectionError {
    cause       @0 :Text;
    retryAfter  @1 :Int64;
    shouldRetry @2 :Bool;
}

struct ConnectionResponse {
    result :union {
        connectionDetails @0 :ConnectionDetails;
        error             @1 :ConnectionError;
    }
}

interface RegistrationServer {
    registerConnection @0 (
        auth        :TunnelAuth,
        tunnelId    :Data,
        connIndex   :UInt8,
        options     :ConnectionOptions,
    ) -> (result :ConnectionResponse);

    unregisterConnection @1 ();
}

struct TunnelAuth {
    accountTag   @0 :Text;
    tunnelSecret @1 :Data;     # raw bytes of the base64-decoded secret
}
