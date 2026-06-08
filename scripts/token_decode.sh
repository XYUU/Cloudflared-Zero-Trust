#!/usr/bin/env bash
# Decode a cloudflared tunnel token and print config file fields.
# Usage: ./token_decode.sh <token>
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <tunnel-token>" >&2
    exit 1
fi

TOKEN="$1"

python3 - "$TOKEN" <<'EOF'
import sys, base64, json

token = sys.argv[1].strip()
pad = (4 - len(token) % 4) % 4
try:
    raw = base64.urlsafe_b64decode(token + '=' * pad)
    obj = json.loads(raw)
except Exception as e:
    print(f"error: failed to decode token: {e}", file=sys.stderr)
    sys.exit(1)

account_tag       = obj.get('a', '')
tunnel_id         = obj.get('t', '')
tunnel_secret_b64 = obj.get('s', '')

if not all([account_tag, tunnel_id, tunnel_secret_b64]):
    print("error: token missing expected fields (a/t/s)", file=sys.stderr)
    sys.exit(1)

print("# Paste these into your cfd config file:")
print(f"account_tag       = {account_tag}")
print(f"tunnel_id         = {tunnel_id}")
print(f"tunnel_secret_b64 = {tunnel_secret_b64}")
EOF
