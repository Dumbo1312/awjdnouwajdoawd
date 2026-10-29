#
# server_grant_signing.py — SERVER-SIDE offline-grant signing (reference).
#
# Paste this into your real backend (/v2/session/init and
# /v2/session/heartbeat). The client (Core/ServerSecureClient.cpp) verifies
# the signature with the PUBLIC key embedded in Utilities/OfflineGrantKey.h —
# it can only VERIFY; only this file's private key can sign.
#
# Grant format (must match the client):
#   payload = compact JSON: {"ts": <unix seconds>, "hwid": "...", "license": "..."}
#   sig     = RSA-2048 PKCS#1 v1.5 SHA-256 over the EXACT payload bytes
#   grant   = base64( payload + "|" + base64(sig) )
#
# Include "offline_grant": <grant> in every /init and /heartbeat response.
#
# Key files: Tools/server_offline_private.pem (KEEP ON SERVER, never ship),
#            Tools/server_offline_public.pem (test/verify only).
#
# Requires: pycryptodome  OR  cryptography.
#

import base64
import json
import time

GRANT_HOURS = 48                 # keep the default in sync with the client
PRIVATE_PEM = "server_offline_private.pem"

try:
    from Crypto.Hash import SHA256
    from Crypto.Signature import pkcs1_15
    from Crypto.PublicKey import RSA
    _PRIVATE_KEY = RSA.import_key(open(PRIVATE_PEM).read())

    def _sign(payload_bytes: bytes) -> bytes:
        return pkcs1_15.new(_PRIVATE_KEY).sign(SHA256.new(payload_bytes))

except ImportError:
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import padding

    _PRIVATE_KEY = serialization.load_pem_private_key(
        open(PRIVATE_PEM, "rb").read(), password=None)

    def _sign(payload_bytes: bytes) -> bytes:
        return _PRIVATE_KEY.sign(payload_bytes, padding.PKCS1v15(), hashes.SHA256())


def make_offline_grant(hwid: str, license_key: str) -> str:
    """Build the signed offline grant for a verified license/hwid pair."""
    payload = json.dumps(
        {"ts": int(time.time()), "hwid": hwid, "license": license_key},
        separators=(",", ":"))
    payload_bytes = payload.encode("utf-8")
    sig_b64 = base64.b64encode(_sign(payload_bytes)).decode("ascii")
    return base64.b64encode(
        (payload + "|" + sig_b64).encode("utf-8")).decode("ascii")


def verify_offline_grant(grant: str, public_pem_path: str) -> bool:
    """Server-side test helper (uses the PUBLIC key)."""
    raw = base64.b64decode(grant).decode("utf-8")
    payload, sig_b64 = raw.rsplit("|", 1)
    from Crypto.PublicKey import RSA as _RSA
    try:
        pub = _RSA.import_key(open(public_pem_path).read())
        pkcs1_15.new(pub).verify(SHA256.new(payload.encode("utf-8")),
                                 base64.b64decode(sig_b64))
        return True
    except Exception:
        return False


if __name__ == "__main__":
    g = make_offline_grant("TESTHWID", "TESTLICENSE")
    print("grant:", g[:80], "...")
    print("self-verify:", verify_offline_grant(g, "server_offline_public.pem"))
    print("tampered verify:", verify_offline_grant(g[:-1] + ("A" if g[-1] != "A" else "B"),
                                                   "server_offline_public.pem"))