import sys
import os
from typing import Callable

# Add project root to path so radio_handle.py can be imported
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../")))

try:
    from radio_handle import RadioHandler, RadioMode
except ImportError as e:
    print(f"[WARNING] Wystąpił błąd podczas ładowania modułu radia: {e}")


class LoRaHardware:
    def __init__(self):
        self.on_receive_callback = None
        self.radio = None

    def initialize(self):
        """Initialize the LoRa radio hardware."""
        print("[HARDWARE LoRa] Inicjalizacja sprzętowa z użyciem pliku radio_handle.py...")

        def internal_callback(data, rssi=None, index=None):
            # radio_handle.py returns data as a joined string of chr(byte) – convert back to bytes
            if self.on_receive_callback and data:
                raw_bytes = bytes([ord(c) for c in data])
                rssi_val = rssi if rssi is not None else 0
                print(f"[HARDWARE Lora Internal] Odbiór surowej przetworzonej ramki (Długość: {len(raw_bytes)}) o RSSI {rssi_val} dBm")
                self.on_receive_callback(raw_bytes, rssi_val)

        try:
            self.radio = RadioHandler(RadioMode.LORA, internal_callback)

            # Set sync word to 0x34 (BEKO_NET_V1 / LoRaWAN public network)
            # pyLoraRFM9x never writes REG_39, so we do it explicitly after init
            REG_39_SYNC_WORD = 0x39
            self.radio.lora_handler._spi_write(REG_39_SYNC_WORD, 0x34)

            # Read back key registers to verify correct configuration
            op_mode  = self.radio.lora_handler._spi_read(0x01)
            sync_word = self.radio.lora_handler._spi_read(REG_39_SYNC_WORD)
            freq_msb = self.radio.lora_handler._spi_read(0x06)
            freq_mid = self.radio.lora_handler._spi_read(0x07)
            freq_lsb = self.radio.lora_handler._spi_read(0x08)
            pa_cfg   = self.radio.lora_handler._spi_read(0x09)

            print(f"[HARDWARE LoRa DIAG] OP_MODE=0x{op_mode:02X}, SYNC_WORD=0x{sync_word:02X} "
                  f"(expected=0x34, {'OK' if sync_word == 0x34 else 'ERROR!'})")

            fstep = 32e6 / 2**19
            freq_hz = ((freq_msb << 16) | (freq_mid << 8) | freq_lsb) * fstep
            print(f"[HARDWARE LoRa DIAG] FREQ={freq_hz/1e6:.3f} MHz (expected=868.500 MHz), "
                  f"PA_CONFIG=0x{pa_cfg:02X} (PA_BOOST={'YES' if pa_cfg & 0x80 else 'NO!'})")

            print("[HARDWARE LoRa] Moduł fizyczny pomyślnie załadowany i włączony!")
        except Exception as e:
            print(f"[HARDWARE LoRa ERROR] Błąd podczas przydzielania gpio dla modułu: {e}")

    def send_frame(self, frame: bytes):
        """Transmit a raw BEKO_NET_V1 frame over LoRa."""
        if self.radio:
            print(f"[HARDWARE LoRa TX] Próba wysyłania, rozmiar={len(frame)} B, hex={frame.hex()}")
            payload_ints = list(frame)
            try:
                tx_ok = self.radio.send(payload_ints)
                if tx_ok is False:
                    # TX_DONE interrupt did not fire within timeout – frame may not have been sent
                    print("[HARDWARE LoRa TX] UWAGA: send() zwróciło False – TX_DONE nie wykryty lub CAD blocked!")
                    try:
                        self.radio.set_mode_rx()  # force radio back to receive mode
                    except Exception:
                        pass
                else:
                    print(f"[HARDWARE LoRa TX] OK – ramka wysłana ({len(frame)} B)")
            except Exception as e:
                print(f"[HARDWARE LoRa TX] BŁĄD podczas wysyłania: {e}")
        else:
            # Hardware not available – simulation mode
            print("[HARDWARE LoRa TX] Symulacja (brak sprzętu) =>", frame.hex())

    def attach_receive_interrupt(self, callback: Callable[[bytes, int], None]):
        """Register the upper-layer callback invoked on every received frame."""
        self.on_receive_callback = callback
        print("[HARDWARE LoRa] Callback nasłuchiwania podpięty pod asynchroniczną pętlę.")


# Singleton instance used across the application
lora_device = LoRaHardware()
