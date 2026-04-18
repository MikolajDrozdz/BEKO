"""
Pełna diagnostyka HMAC bajt-po-bajcie by porównać z oczekiwaniami STM32.

Uruchom: python3 gateway/debug_hmac2.py

Ten skrypt odtwarza dokładnie to co robi Gateway i to co robi STM32 przy weryfikacji.
"""
import struct, hmac, hashlib

LAVIET_SHARED_V1  = b"LAVIET_SHARED_V1"
LAVIET_GATEWAY_ID = 0x0001
LAVIET_BROADCAST  = 0xFFFF

# ── klucze (security_main.c: LV1K || domain_id(2B) || 0x00 || selector) ─────
def derive(base_key, domain_id, selector):
    info = b"LV1K" + struct.pack(">H", domain_id) + bytes([0x00, selector])
    print(f"  KDF info  : {info.hex()}  (LV1K + domain_id={domain_id:#06x} + 0x00 + sel={selector})")
    key = hmac.new(base_key, info, hashlib.sha256).digest()
    print(f"  → key[{selector}]   : {key.hex()}")
    return key

# ── MAC (laviet_frame_build_mac_input + laviet_hmac_sha256) ──────────────────
def mac(key, mac_input):
    tag = hmac.new(key, mac_input, hashlib.sha256).digest()
    return tag

# ════════════════════════════════════════════════════════════════════════════
print("━"*64)
print("  PAIR_REQ  broadcast  (Gateway → STM32)")
print("━"*64)

# --- Użyjemy stałego payloadu z README.md dla pełnej powtarzalności ---
msg_id   = 0x1001
counter  = 0x00000001
payload  = b"12345678"          # 8 B, plaintext (brak ENCRYPTED)
src_id   = LAVIET_GATEWAY_ID   # 0x0001
dst_id   = LAVIET_BROADCAST     # 0xFFFF
flags    = 0x28                 # PAIRING | BROADCAST
ver_type = 0x14                 # version=1, type=PAIR_REQ=4

print(f"\n[1] Pola ramki:")
print(f"  ver_type  = {ver_type:#04x}  (ver=1, type=PAIR_REQ=4)")
print(f"  flags     = {flags:#04x}  (PAIRING=0x08 | BROADCAST=0x20)")
print(f"  src_id    = {src_id:#06x}")
print(f"  dst_id    = {dst_id:#06x}")
print(f"  msg_id    = {msg_id:#06x}")
print(f"  counter   = {counter:#010x}")
print(f"  payload   = {payload.hex()}  ({len(payload)} B, plaintext – brak flagi ENCRYPTED)")

print(f"\n[2] Wyprowadzanie klucza HMAC (security_main_get_frame_keys):")
print(f"  broadcast → domain_id = 0xFFFF")
hmac_key = derive(LAVIET_SHARED_V1, 0xFFFF, selector=2)

print(f"\n[3] Budowanie mac_input (laviet_frame_build_mac_input):")
header = struct.pack(">BBHHHIB",
    ver_type, flags, src_id, dst_id, msg_id, counter, len(payload))
mac_input = header + payload
print(f"  header    : {header.hex()}  ({len(header)} B)")
print(f"  payload   : {payload.hex()}  ({len(payload)} B, BRAK szyfrowania)")
print(f"  mac_input : {mac_input.hex()}  ({len(mac_input)} B)")

print(f"\n[4] Obliczanie MAC:")
mac_tag = mac(hmac_key, mac_input)
print(f"  mac_tag   : {mac_tag.hex()}")

full_frame = mac_input + mac_tag
print(f"\n[5] Pełna ramka ({len(full_frame)} B):")
print("  " + " ".join(f"{b:02X}" for b in full_frame))

print(f"\n[6] Weryfikacja względem README.md przykład A:")
expected = bytes.fromhex("27e0229407535660f6f0249eb9b3560c1568a27520266aa06f15c1667959ddf9")
if mac_tag == expected:
    print("  ✅  MAC = README.md – logika poprawna")
else:
    print("  ❌  MAC ≠ README.md – BŁĄD W LOGICE!")
    print(f"  Expected : {expected.hex()}")
    print(f"  Got      : {mac_tag.hex()}")

# ════════════════════════════════════════════════════════════════════════════
print()
print("━"*64)
print("  PARA ŻYWEGO ZDARZENIA (tak jak Gateway spakuje przy time()-based msg_id)")
print("━"*64)

import time, os
msg_id2  = int(time.time() % 65535)
counter2 = 0
payload2 = os.urandom(8)
flags2   = 0x28
dst2     = LAVIET_BROADCAST

print(f"\n  msg_id  = {msg_id2:#06x}")
print(f"  counter = {counter2}")
print(f"  payload = {payload2.hex()}")

hmac_key2 = derive(LAVIET_SHARED_V1, 0xFFFF, selector=2)
header2   = struct.pack(">BBHHHIB",
    ver_type, flags2, LAVIET_GATEWAY_ID, dst2, msg_id2, counter2, len(payload2))
mac_input2 = header2 + payload2
mac_tag2   = mac(hmac_key2, mac_input2)
frame2     = mac_input2 + mac_tag2

print(f"\n  header    : {header2.hex()}")
print(f"  mac_input : {mac_input2.hex()}")
print(f"  mac_tag   : {mac_tag2.hex()}")
print(f"\n  Ramka wysyłana ({len(frame2)} B):")
print("  " + " ".join(f"{b:02X}" for b in frame2))

# ════════════════════════════════════════════════════════════════════════════
print()
print("━"*64)
print("  SYMULACJA STM32 – co STM zrobi z powyższą ramką")
print("━"*64)

# STM32 parse:
in_ = frame2
stm_ver_type  = in_[0]
stm_flags     = in_[1]
stm_src_id    = struct.unpack(">H", in_[2:4])[0]
stm_dst_id    = struct.unpack(">H", in_[4:6])[0]
stm_msg_id    = struct.unpack(">H", in_[6:8])[0]
stm_counter   = struct.unpack(">I", in_[8:12])[0]
stm_plen      = in_[12]
stm_payload   = in_[13:13+stm_plen]
stm_mac_tag   = in_[13+stm_plen:13+stm_plen+32]

print(f"\n  Parsowanie ramki przez STM:")
print(f"  dst_id    = {stm_dst_id:#06x}  → is_broadcast = {stm_dst_id == LAVIET_BROADCAST}")
print(f"  frame_type= PAIR_REQ → use_pair_link = False")

stm_peer_id   = LAVIET_BROADCAST if stm_dst_id == LAVIET_BROADCAST else stm_src_id
stm_domain_id = LAVIET_BROADCAST if stm_peer_id == LAVIET_BROADCAST else min(0x77CD, stm_peer_id)
print(f"  peer_id   = {stm_peer_id:#06x}")
print(f"  domain_id = {stm_domain_id:#06x}")

stm_hmac_key = derive(LAVIET_SHARED_V1, stm_domain_id, selector=2)

stm_mac_input = struct.pack(">BBHHHIB",
    stm_ver_type, stm_flags, stm_src_id, stm_dst_id,
    stm_msg_id, stm_counter, stm_plen) + stm_payload
stm_expected_mac = mac(stm_hmac_key, stm_mac_input)

print(f"\n  stm mac_input : {stm_mac_input.hex()}")
print(f"  stm expected  : {stm_expected_mac.hex()}")
print(f"  received mac  : {stm_mac_tag.hex()}")

if stm_expected_mac == stm_mac_tag:
    print("\n  ✅  STM PRZYJĄŁBY tę ramkę (HMAC OK)")
else:
    print("\n  ❌  STM ODRZUCIŁBY tę ramkę (HMAC FAILED)")
    mismatches = [i for i in range(32) if stm_expected_mac[i] != stm_mac_tag[i]]
    print(f"     Pierwsze niezgodne bajty: pozycje {mismatches[:5]}")
