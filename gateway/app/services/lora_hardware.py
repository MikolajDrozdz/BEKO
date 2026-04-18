import os
import sys
from typing import Callable, Optional

# Add project root to path so radio_handle.py can be imported.
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../")))

try:
    from radio_handle import RadioHandler, RadioMode
except ImportError as e:
    print(f"[WARNING] Failed to load radio module: {e}")
    RadioHandler = None
    RadioMode = None


class LoRaHardware:
    def __init__(self):
        self.on_receive_callback = None
        self.radio: Optional[RadioHandler] = None
        self.last_error: Optional[str] = None

    def is_ready(self) -> bool:
        return self.radio is not None

    def get_last_error(self) -> Optional[str]:
        return self.last_error

    def initialize(self):
        print("[HARDWARE LoRa] Initializing hardware via radio_handle.py...")
        self.last_error = None

        def internal_callback(data, rssi=None, index=None):
            if self.on_receive_callback and data:
                raw_bytes = bytes([ord(c) for c in data])
                rssi_val = rssi if rssi is not None else 0
                print(
                    f"[HARDWARE LoRa RX] Received raw frame len={len(raw_bytes)} RSSI={rssi_val} dBm"
                )
                self.on_receive_callback(raw_bytes, rssi_val)

        try:
            if RadioHandler is None or RadioMode is None:
                raise RuntimeError("radio_handle.py is unavailable")

            self.radio = RadioHandler(RadioMode.LORA, internal_callback)

            reg_39_sync_word = 0x39
            self.radio.lora_handler._spi_write(reg_39_sync_word, 0x34)

            op_mode = self.radio.lora_handler._spi_read(0x01)
            sync_word = self.radio.lora_handler._spi_read(reg_39_sync_word)
            freq_msb = self.radio.lora_handler._spi_read(0x06)
            freq_mid = self.radio.lora_handler._spi_read(0x07)
            freq_lsb = self.radio.lora_handler._spi_read(0x08)
            pa_cfg = self.radio.lora_handler._spi_read(0x09)

            print(
                f"[HARDWARE LoRa DIAG] OP_MODE=0x{op_mode:02X} SYNC_WORD=0x{sync_word:02X} "
                f"(expected=0x34, {'OK' if sync_word == 0x34 else 'ERROR'})"
            )

            fstep = 32e6 / 2**19
            freq_hz = ((freq_msb << 16) | (freq_mid << 8) | freq_lsb) * fstep
            print(
                f"[HARDWARE LoRa DIAG] FREQ={freq_hz/1e6:.3f} MHz (expected=868.500 MHz) "
                f"PA_CONFIG=0x{pa_cfg:02X} (PA_BOOST={'YES' if pa_cfg & 0x80 else 'NO'})"
            )

            print("[HARDWARE LoRa] Radio initialized")
            return True
        except Exception as e:
            self.radio = None
            self.last_error = str(e)
            print(f"[HARDWARE LoRa ERROR] Failed to initialize radio: {e}")
            return False

    def send_frame(self, frame: bytes) -> bool:
        if not self.radio:
            self.last_error = "radio is not initialized"
            print("[HARDWARE LoRa TX] ERROR: refusing TX because radio is not initialized")
            return False

        print(f"[HARDWARE LoRa TX] len={len(frame)} hex={frame.hex()}")
        payload_ints = list(frame)
        try:
            tx_ok = self.radio.send(payload_ints)
            if tx_ok is False:
                self.last_error = "TX_DONE missing or CAD blocked"
                print("[HARDWARE LoRa TX] TX_DONE missing or CAD blocked")
                try:
                    self.radio.set_mode_rx()
                except Exception:
                    pass
                return False

            self.last_error = None
            print(f"[HARDWARE LoRa TX] OK ({len(frame)} B)")
            return True
        except Exception as e:
            self.last_error = str(e)
            print(f"[HARDWARE LoRa TX] ERROR: {e}")
            return False

    def attach_receive_interrupt(self, callback: Callable[[bytes, int], None]):
        self.on_receive_callback = callback
        print("[HARDWARE LoRa] RX callback attached")


lora_device = LoRaHardware()
