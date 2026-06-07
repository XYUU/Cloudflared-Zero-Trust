#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# End-to-end smoke test for cfd + Cloudflare WARP.
#
# Run on the host that has cfd installed. Assumes:
#   - cloudflared is also available (to create the tunnel + push the route).
#   - You are logged in to a Cloudflare Zero Trust org.
#   - WARP is installed on a second machine, logged into the same org.
#
# Steps performed by this script:
#   1. Create a one-shot tunnel with cloudflared, save credentials JSON.
#   2. Generate /tmp/cfd-e2e.ini from that JSON + the CIDR you pass on cmdline.
#   3. Push the CIDR route into the Zero Trust dashboard.
#   4. Spin up cfd in the background (--verbose), wait for "registered at edge".
#   5. Start a local listener inside the CIDR (python http.server on tun_local).
#   6. Print the next-step instructions for the WARP client.
#   7. On exit (Ctrl-C or fail), tear everything down idempotently.

set -euo pipefail

CIDR="${1:-10.99.0.0/24}"
TUN_LOCAL="${2:-10.99.0.1/32}"
TUN_NAME="${TUN_NAME:-cfd0}"
TUN_TEST_PORT="${TUN_TEST_PORT:-8080}"
WORKDIR="$(mktemp -d -t cfd-e2e.XXXXXX)"
TUNNEL_NAME="cfd-e2e-$(date +%s)"

log()  { printf '\033[1;34m[e2e]\033[0m %s\n' "$*" >&2; }
fail() { printf '\033[1;31m[e2e:FAIL]\033[0m %s\n' "$*" >&2; exit 1; }

require() {
    command -v "$1" >/dev/null 2>&1 || fail "missing dependency: $1"
}
require cloudflared
require jq
require cfd
require python3

CFD_PID=""
SRV_PID=""
TUNNEL_ID=""

cleanup() {
    set +e
    log "tearing down"
    [[ -n "$SRV_PID" ]] && kill "$SRV_PID" 2>/dev/null
    [[ -n "$CFD_PID" ]] && kill "$CFD_PID" 2>/dev/null
    if [[ -n "$TUNNEL_ID" ]]; then
        cloudflared tunnel route ip delete "$CIDR" 2>/dev/null
        cloudflared tunnel delete -f "$TUNNEL_ID"  2>/dev/null
    fi
    rm -rf "$WORKDIR"
}
trap cleanup EXIT INT TERM

log "creating tunnel $TUNNEL_NAME"
cloudflared tunnel create --output json "$TUNNEL_NAME" > "$WORKDIR/create.json"
TUNNEL_ID=$(jq -r .id        "$WORKDIR/create.json")
TUNNEL_SECRET=$(jq -r .secret "$WORKDIR/create.json")
ACCOUNT_TAG=$(jq -r .account_tag 2>/dev/null < ~/.cloudflared/cert.pem.json \
              || cloudflared tunnel info --output json "$TUNNEL_ID" | jq -r .account_tag)

log "tunnel id  = $TUNNEL_ID"
log "writing config to $WORKDIR/cfd.ini"
cat > "$WORKDIR/cfd.ini" <<EOF
tunnel_id         = $TUNNEL_ID
account_tag       = $ACCOUNT_TAG
tunnel_secret_b64 = $TUNNEL_SECRET
tun_name          = $TUN_NAME
tun_local         = $TUN_LOCAL
route             = $CIDR
EOF

log "registering CIDR $CIDR at edge"
cloudflared tunnel route ip add "$CIDR" "$TUNNEL_ID"

log "starting cfd"
cfd --config "$WORKDIR/cfd.ini" --verbose > "$WORKDIR/cfd.log" 2>&1 &
CFD_PID=$!

# Wait for the "registered at edge" line, up to 20 seconds.
for i in $(seq 1 40); do
    if grep -q "registered at edge" "$WORKDIR/cfd.log"; then break; fi
    sleep 0.5
    kill -0 "$CFD_PID" 2>/dev/null || fail "cfd died: $(tail -n 30 $WORKDIR/cfd.log)"
done
grep -q "registered at edge" "$WORKDIR/cfd.log" || fail "registration timeout"

log "spawning HTTP listener on $TUN_LOCAL:$TUN_TEST_PORT"
LISTEN_IP="${TUN_LOCAL%/*}"
python3 -m http.server "$TUN_TEST_PORT" --bind "$LISTEN_IP" \
        --directory "$WORKDIR" > "$WORKDIR/http.log" 2>&1 &
SRV_PID=$!
sleep 0.5
kill -0 "$SRV_PID" 2>/dev/null || fail "http listener failed to start"

cat <<EOF

=========================================================================
  cfd e2e is up. To verify with a WARP client on another machine:

    1. Settings -> Split Tunnel -> Include  $CIDR
    2. curl http://$LISTEN_IP:$TUN_TEST_PORT/cfd.ini
       (you should see this host's config file)

  Live logs:
    cfd:  tail -f $WORKDIR/cfd.log
    http: tail -f $WORKDIR/http.log

  Ctrl-C to tear down (tunnel will be deleted, route removed).
=========================================================================

EOF

wait "$CFD_PID"
