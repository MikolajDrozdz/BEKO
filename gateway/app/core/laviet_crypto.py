import hashlib
import hmac
import struct

from Crypto.Cipher import AES
from Crypto.Util import Counter

LAVIET_SHARED_V1 = b"LAVIET_SHARED_V1"


def security_peer_link_key_derive(local_node_id: int, peer_node_id: int, code: bytes) -> bytes:
    lo = min(local_node_id, peer_node_id)
    hi = max(local_node_id, peer_node_id)

    info = bytearray(b"SEC:PAIR:V1")
    info.extend(struct.pack(">II", lo, hi))
    return hmac.new(code, bytes(info), hashlib.sha256).digest()[:16]


def derive_unicast_base_key(local_node_id: int, peer_node_id: int, code: bytes) -> bytes:
    return security_peer_link_key_derive(local_node_id, peer_node_id, code)


def _derive_subkey(base_key: bytes, domain_id: int, selector: int) -> bytes:
    msg = b"LV1K" + struct.pack(">H", domain_id) + b"\x00" + bytes([selector])
    return hmac.new(base_key, msg, hashlib.sha256).digest()


def get_aes_key(base_key: bytes, domain_id: int) -> bytes:
    return _derive_subkey(base_key, domain_id, 1)[:16]


def get_hmac_key(base_key: bytes, domain_id: int) -> bytes:
    return _derive_subkey(base_key, domain_id, 2)


def laviet_aes_ctr_crypt(
    data: bytes,
    aes_key: bytes,
    src_id: int,
    dst_id: int,
    msg_id: int,
    counter: int,
) -> bytes:
    if not data:
        return b""

    prefix = struct.pack(
        ">4sHHHI",
        b"LV1\x00",
        src_id & 0xFFFF,
        dst_id & 0xFFFF,
        msg_id & 0xFFFF,
        counter & 0xFFFFFFFF,
    )
    ctr = Counter.new(16, prefix=prefix, initial_value=0)
    return AES.new(aes_key, AES.MODE_CTR, counter=ctr).encrypt(data)


def laviet_generate_mac(hmac_key: bytes, header: bytes, cipher_payload: bytes) -> bytes:
    return hmac.new(hmac_key, header + cipher_payload, hashlib.sha256).digest()


def laviet_mac_equal(left: bytes, right: bytes) -> bool:
    return hmac.compare_digest(left, right)
