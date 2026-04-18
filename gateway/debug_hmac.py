"""
Skrypt diagnostyczny – odtwarza dokładnie to co Gateway wysyła
i weryfikuje HMAC tak jak robi to STM32.

Uruchom lokalnie (bez Raspberry Pi):
  python3 gateway/debug_hmac.py
"""
import struct
import hmac
import hashlib

# ---- stałe z laviet_frame.py ----
LAVIET_FRAME_VERSION = 1
LAVIET_GATEWAY_ID    = 0x0001
LAVIET_BROADCAST_ID  = 0xFFFF
LAVIET_FLAG_PAIRING  = (1 << 3)  # 0x08
LAVIET_FLAG_BROADCAST = (1 << 5) # 0x20
LAVIET_PAIR_TYPE     = 4         # PAIR_REQ

# ---- klucz główny ----
LAVIET_SHARED_V1 = b"LAVIET_SHARED_V1"

# ---- funkcje z laviet_crypto.py ----

def _derive_subkey(base_key: bytes, domain_id: int, selector: int) -> bytes:
    """HMAC-SHA256 nad LV1K || domain_id(2B BE) || 0x00 || selector"""
    info = b"LV1K" + struct.pack(">H", domain_id) + b"\x00" + bytes([selector])
    return hmac.new(base_key, info, hashlib.sha256).digest()

def get_hmac_key(base_key: bytes, domain_id: int) -> bytes:
    return _derive_subkey(base_key, domain_id, 2)

def laviet_generate_mac(hmac_key: bytes, header_and_payload: bytes) -> bytes:
    return hmac.new(hmac_key, header_and_payload, hashlib.sha256).digest()


# ========== SYMULACJA GATEWAY ==========

print("=" * 60)
print("GATEWAY wysyła PAIR_REQ (broadcast)")
print("=" * 60)

target_node_id = LAVIET_BROADCAST_ID
msg_id = 0x1001   # statyczny dla testu (by wyniki były powtarzalne)
counter = 0x00000001
payload = b"12345678"   # 8 bajtów - przykład z README.md

# domain_id
domain_id = LAVIET_BROADCAST_ID  # bo target == 0xFFFF
hmac_key = get_hmac_key(LAVIET_SHARED_V1, domain_id)
print(f"domain_id : 0x{domain_id:04X}")
print(f"hmac_key  : {hmac_key.hex()}")

# zbuduj header
flags = LAVIET_FLAG_PAIRING | LAVIET_FLAG_BROADCAST   # 0x28
ver_type = ((LAVIET_FRAME_VERSION & 0x0F) << 4) | (LAVIET_PAIR_TYPE & 0x0F)  # 0x14
header = struct.pack(">BB HHHI B",
    ver_type, flags,
    LAVIET_GATEWAY_ID, target_node_id,
    msg_id, counter,
    len(payload)
)
print(f"\nheader    : {header.hex()}")
print(f"payload   : {payload.hex()}")

# mac_input = header + payload
mac_input = header + payload
print(f"mac_input : {mac_input.hex()}")

mac_tag = laviet_generate_mac(hmac_key, mac_input)
print(f"mac_tag   : {mac_tag.hex()}")

final_frame = mac_input + mac_tag
print(f"\nfull_frame ({len(final_frame)} B):")
print(' '.join(f'{b:02X}' for b in final_frame))

# ---- Porównanie z README.md ----
print("\n" + "=" * 60)
print("OCZEKIWANE z README.md (Przykład A):")
expected_mac = bytes.fromhex(
    "27E022940753566 0F6F0249EB9B3560C"
    "15 68A27520266AA06F15C1667959DDF9".replace(" ", "")
)
# Znormalizuj – usuń spacje z hex z README
readme_mac_hex = "27E0229407535660F6F0249EB9B3560C1568A27520266AA06F15C1667959DDF9"
if mac_tag.hex().upper() == readme_mac_hex.upper():
    print("✅ MAC ZGADZA SIĘ z README.md!")
else:
    print("❌ MAC RÓŻNI SIĘ od README.md")
    print(f"   Oczekiwano : {readme_mac_hex}")
    print(f"   Wyliczono  : {mac_tag.hex().upper()}")

# ========== SYMULACJA STM32 ========== 
print("\n" + "=" * 60)
print("STM32 weryfikuje odebrany PAIR_REQ:")
print("=" * 60)
# STM: peer_id = dst_id == 0xFFFF -> broadcast -> domain_id = 0xFFFF
# local_id = node_id (nie ma znaczenia dla broadcastu)
stm_node_id = 0x77CD   # przykładowe Node ID ze STM32
peer_id = target_node_id  # 0xFFFF bo dst_id == 0xFFFF
stm_domain_id = LAVIET_BROADCAST_ID  # bo peer_id == 0xFFFF
stm_hmac_key = get_hmac_key(LAVIET_SHARED_V1, stm_domain_id)

print(f"STM32 node_id  : 0x{stm_node_id:04X}")
print(f"STM32 domain_id: 0x{stm_domain_id:04X}")
print(f"STM32 hmac_key : {stm_hmac_key.hex()}")

stm_expected = laviet_generate_mac(stm_hmac_key, mac_input)
print(f"STM32 expected : {stm_expected.hex()}")

if stm_expected == mac_tag:
    print("\n✅ STM32 PRZYJĄŁBY tę ramkę (HMAC OK)")
else:
    print("\n❌ STM32 ODRZUCIŁBY tę ramkę (HMAC FAILED)")
    print("   GW  wysłał : " + mac_tag.hex())
    print("   STM oczekuje: " + stm_expected.hex())
