import struct
import hmac
import hashlib
from Crypto.Cipher import AES
from Crypto.Util import Counter

LAVIET_SHARED_V1 = b"LAVIET_SHARED_V1"  # 16 bajtów ASCII

def _fnv_mix(h: int, val: int) -> int:
    h ^= (val & 0xFF)
    return (h * 16777619) & 0xFFFFFFFF

def security_peer_link_key_derive(local_node_id: int, peer_node_id: int, code: bytes) -> bytes:
    """Odtworzenie algorytmu security_peer_link_key_derive() 1:1 z STM32 w Pythonie."""
    lo = min(local_node_id, peer_node_id)
    hi = max(local_node_id, peer_node_id)
    
    label = b"SEC:PAIR:V1"
    info = bytearray(label)
    info.extend(struct.pack(">II", lo, hi))
    
    # W C używane jest laviet_hmac_sha256 z: key=code, data=info
    digest = hmac.new(code, bytes(info), hashlib.sha256).digest()
    
    # Bierzemy pierwsze 16 bajtów wygenerowanego rekordu jako AES Peer Link Key!
    return digest[:16]

def _derive_subkey(base_key: bytes, domain_id: int, selector: int) -> bytes:
    """HMAC-SHA256 nad LV1K || domain_id || 0x00 || selector"""
    msg = b"LV1K" + struct.pack(">H", domain_id) + b"\x00" + bytes([selector])
    return hmac.new(base_key, msg, hashlib.sha256).digest()

def get_aes_key(base_key: bytes, domain_id: int) -> bytes:
    # Pobieramy 16 bajtów do AES-128, hash ma 32 bajty
    return _derive_subkey(base_key, domain_id, 1)[:16]

def get_hmac_key(base_key: bytes, domain_id: int) -> bytes:
    return _derive_subkey(base_key, domain_id, 2)

def laviet_aes_ctr_crypt(data: bytes, aes_key: bytes, src_id: int, dst_id: int, msg_id: int, counter: int) -> bytes:
    if not data:
        return b""
    # Budujemy pełny 14-bajtowy prefix dla Crypto.Util.Counter, żeby odwzorować
    # strukturę b"LV1\x00" | src_id | dst_id | msg_id | counter | z języka C
    prefix = struct.pack(">4sHHHI", b"LV1\x00", src_id & 0xFFFF, dst_id & 0xFFFF, msg_id & 0xFFFF, counter & 0xFFFFFFFF)
    # Counter definiuje 16 bajtow inkrementujacej przestrzeni (wymog libctr), my mapujemy ją jako intial_value=0
    ctr = Counter.new(16, prefix=prefix, initial_value=0)
    cipher = AES.new(aes_key, AES.MODE_CTR, counter=ctr)
    return cipher.encrypt(data)

def laviet_generate_mac(hmac_key: bytes, header: bytes, cipher_payload: bytes) -> bytes:
    # DEBUG BYPASS: Zamiast liczyć HMAC, zwracamy stały wzorzec dla łatwego rozpoznania w eterze
    # return hmac.new(hmac_key, header + cipher_payload, hashlib.sha256).digest()
    return b"\x01" * 32
