import struct
from dataclasses import dataclass
from typing import Optional

LAVIET_FRAME_VERSION = 1
LAVIET_MAX_PAYLOAD = 16
LAVIET_MAC_TAG_LEN = 32
LAVIET_FRAME_HEADER_LEN = 13
LAVIET_FRAME_MIN_LEN = 45
LAVIET_FRAME_MAX_LEN = 61

LAVIET_GATEWAY_ID = 0x0001
LAVIET_BROADCAST_ID = 0xFFFF

# Typy ramek
class LavietType:
    DATA = 1
    ACK = 2
    RESP = 3
    PAIR_REQ = 4
    PAIR_RESP = 5
    CFG = 6
    COUNTER_SYNC = 7
    KEY_ROTATE = 8
    ERROR = 9

# Flagi z stm32 laviet_frame.h
LAVIET_FLAG_ENCRYPTED        = (1 << 0)
LAVIET_FLAG_ACK_REQUIRED     = (1 << 1)
LAVIET_FLAG_IS_ACK           = (1 << 2)
LAVIET_FLAG_PAIRING          = (1 << 3)
LAVIET_FLAG_CONFIG_ACCESS    = (1 << 4)
LAVIET_FLAG_BROADCAST        = (1 << 5)
LAVIET_FLAG_COUNTER_OVERRIDE = (1 << 6)
LAVIET_FLAG_KEY_UPDATE       = (1 << 7)

@dataclass
class LavietFrame:
    type: int
    flags: int
    src_id: int
    dst_id: int
    msg_id: int
    counter: int
    payload_len: int
    payload: bytes
    mac_tag: Optional[bytes] = None

class LavietFrameBuilder:
    @staticmethod
    def _ver_type(frame_type: int) -> int:
        return ((LAVIET_FRAME_VERSION & 0x0F) << 4) | (frame_type & 0x0F)

    @staticmethod
    def build_mac_input(f: LavietFrame) -> bytes:
        payload = bytes(f.payload)
        payload_len = f.payload_len
        if payload_len != len(payload):
            raise ValueError(
                f"payload_len ({payload_len}) nie pasuje do faktycznej dlugosci ({len(payload)})"
            )
        if payload_len > LAVIET_MAX_PAYLOAD:
            raise ValueError("Payload zbyt duży")

        return struct.pack(
            ">BBHHHIB",
            LavietFrameBuilder._ver_type(f.type),
            f.flags,
            f.src_id,
            f.dst_id,
            f.msg_id,
            f.counter,
            payload_len,
        ) + payload

    @staticmethod
    def parse_frame(frame: bytes) -> LavietFrame:
        if len(frame) < LAVIET_FRAME_MIN_LEN or len(frame) > LAVIET_FRAME_MAX_LEN:
            raise ValueError(f"Błędna długość ramki: {len(frame)} to nie [45, 61]")
            
        ver_type = frame[0]
        version = (ver_type >> 4) & 0x0F
        if version != LAVIET_FRAME_VERSION:
            raise ValueError(f"Nieobsługiwana wersja ramki: {version}")
            
        msg_type = ver_type & 0x0F
        flags = frame[1]
        
        src_id, dst_id, msg_id, counter, payload_len = struct.unpack(">HHHIB", frame[2:13])
        
        if payload_len > LAVIET_MAX_PAYLOAD:
            raise ValueError(f"Odrzucono, dlugosc payloadu przekracza maks ({payload_len} > 16)")
            
        expected_len = LAVIET_FRAME_MIN_LEN + payload_len
        if len(frame) != expected_len:
            raise ValueError(f"Faktyczna dlugosc ramki ({len(frame)}) nie pasuje do zadeklarowanej ({expected_len})")
            
        payload = frame[13:13+payload_len]
        mac_tag = frame[13+payload_len:13+payload_len+LAVIET_MAC_TAG_LEN]
        
        if src_id == 0x0000:
            raise ValueError("Odrzucono ramke: adres źródłowy 0x0000 jest nieważny")
            
        return LavietFrame(
            type=msg_type,
            flags=flags,
            src_id=src_id,
            dst_id=dst_id,
            msg_id=msg_id,
            counter=counter,
            payload_len=payload_len,
            payload=payload,
            mac_tag=mac_tag
        )

    @staticmethod
    def build_frame(f: LavietFrame) -> bytes:
        out = bytearray(LavietFrameBuilder.build_mac_input(f))
        if f.mac_tag:
            if len(f.mac_tag) != LAVIET_MAC_TAG_LEN:
                raise ValueError("mac_tag musi miec 32 B")
            out.extend(f.mac_tag)
            
        return bytes(out)
