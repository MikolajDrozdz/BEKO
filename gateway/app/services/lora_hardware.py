import os
import sys
import threading
import time
from typing import Any, Callable, Optional, Tuple

# Add project root to path so radio_handle.py can be imported when present.
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../")))

try:
    from radio_handle import RadioHandler, RadioMode
except ImportError as e:
    print(f"[WARNING] Failed to load external radio module: {e}")
    RadioHandler = None
    RadioMode = None

try:
    import radio_defines as legacy_radio_defines
except ImportError:
    legacy_radio_defines = None

try:
    import spidev
except ImportError as e:
    print(f"[WARNING] Failed to import spidev: {e}")
    spidev = None

try:
    import lgpio
except ImportError:
    lgpio = None

try:
    import RPi.GPIO as GPIO
except ImportError:
    GPIO = None


def _env_int(name: str, default: int) -> int:
    raw = os.getenv(name)
    if raw is None or raw == "":
        return default
    try:
        return int(raw, 0)
    except ValueError:
        return default


def _legacy_freq_hz(default_hz: int) -> int:
    if legacy_radio_defines is None:
        return default_hz

    legacy_freq = getattr(legacy_radio_defines, "LORA_FREQ", None)
    if legacy_freq is None:
        return default_hz

    try:
        legacy_float = float(legacy_freq)
    except (TypeError, ValueError):
        return default_hz

    if legacy_float > 1000000:
        return int(legacy_float)
    return int(legacy_float * 1000000)


class _GpioHelper:
    def __init__(self, reset_pin: int):
        self.reset_pin = reset_pin
        self.backend = "none"
        self.handle: Optional[int] = None

    def initialize(self) -> None:
        if self.reset_pin < 0:
            return

        if lgpio is not None:
            self.handle = lgpio.gpiochip_open(_env_int("LAVIET_GPIO_CHIP", 0))
            lgpio.gpio_claim_output(self.handle, self.reset_pin, 1)
            self.backend = "lgpio"
            return

        if GPIO is not None:
            GPIO.setwarnings(False)
            GPIO.setmode(GPIO.BCM)
            GPIO.setup(self.reset_pin, GPIO.OUT, initial=GPIO.HIGH)
            self.backend = "RPi.GPIO"
            return

    def pulse_reset(self) -> None:
        if self.reset_pin < 0:
            return

        if self.backend == "lgpio" and self.handle is not None:
            lgpio.gpio_write(self.handle, self.reset_pin, 0)
            time.sleep(0.01)
            lgpio.gpio_write(self.handle, self.reset_pin, 1)
            time.sleep(0.01)
        elif self.backend == "RPi.GPIO":
            GPIO.output(self.reset_pin, GPIO.LOW)
            time.sleep(0.01)
            GPIO.output(self.reset_pin, GPIO.HIGH)
            time.sleep(0.01)


class _SX1276LoRaRadio:
    REG_FIFO = 0x00
    REG_OP_MODE = 0x01
    REG_FRF_MSB = 0x06
    REG_FRF_MID = 0x07
    REG_FRF_LSB = 0x08
    REG_PA_CONFIG = 0x09
    REG_LNA = 0x0C
    REG_FIFO_ADDR_PTR = 0x0D
    REG_FIFO_TX_BASE_ADDR = 0x0E
    REG_FIFO_RX_BASE_ADDR = 0x0F
    REG_FIFO_RX_CURRENT_ADDR = 0x10
    REG_IRQ_FLAGS = 0x12
    REG_RX_NB_BYTES = 0x13
    REG_PKT_SNR_VALUE = 0x19
    REG_PKT_RSSI_VALUE = 0x1A
    REG_MODEM_CONFIG1 = 0x1D
    REG_MODEM_CONFIG2 = 0x1E
    REG_PREAMBLE_MSB = 0x20
    REG_PREAMBLE_LSB = 0x21
    REG_PAYLOAD_LENGTH = 0x22
    REG_MODEM_CONFIG3 = 0x26
    REG_SYNC_WORD = 0x39
    REG_DIO_MAPPING1 = 0x40
    REG_VERSION = 0x42

    MODE_LONG_RANGE = 0x80
    MODE_SLEEP = 0x00
    MODE_STDBY = 0x01
    MODE_TX = 0x03
    MODE_RX_CONTINUOUS = 0x05

    IRQ_RX_DONE = 0x40
    IRQ_PAYLOAD_CRC_ERROR = 0x20
    IRQ_TX_DONE = 0x08

    def __init__(self):
        if spidev is None:
            raise RuntimeError("spidev is unavailable")

        default_spi_bus = getattr(legacy_radio_defines, "SPI_PORT", 0) if legacy_radio_defines else 0
        default_spi_cs = getattr(legacy_radio_defines, "SPI_CHANNEL", 1) if legacy_radio_defines else 1
        default_reset_pin = getattr(legacy_radio_defines, "RESET_PIN", 25) if legacy_radio_defines else 25
        default_dio0_pin = getattr(legacy_radio_defines, "INTERRUPT_PIN", 22) if legacy_radio_defines else 22
        default_tx_power = getattr(legacy_radio_defines, "LORA_POWER", 17) if legacy_radio_defines else 17
        default_sync_word = getattr(legacy_radio_defines, "LORA_SYNC_WORD", 0x34) if legacy_radio_defines else 0x34

        self.spi_bus = _env_int("LAVIET_SPI_BUS", int(default_spi_bus))
        self.spi_cs = _env_int("LAVIET_SPI_CS", int(default_spi_cs))
        self.spi_hz = _env_int("LAVIET_SPI_HZ", 5000000)
        self.reset_pin = _env_int("LAVIET_GPIO_RESET", int(default_reset_pin))
        self.dio0_pin = _env_int("LAVIET_GPIO_DIO0", int(default_dio0_pin))
        self.freq_hz = _env_int("LAVIET_LORA_FREQ_HZ", _legacy_freq_hz(868500000))
        self.tx_power = _env_int("LAVIET_LORA_TX_POWER", int(default_tx_power))
        self.sync_word = _env_int("LAVIET_LORA_SYNC_WORD", int(default_sync_word))
        self.poll_interval = max(5, _env_int("LAVIET_RX_POLL_MS", 20)) / 1000.0
        self.spi: Any = None
        self.gpio = _GpioHelper(self.reset_pin)
        self.lock = threading.RLock()
        self.ready = False

    def _read_reg(self, reg: int) -> int:
        resp = self.spi.xfer2([reg & 0x7F, 0x00])
        return resp[1]

    def _write_reg(self, reg: int, value: int) -> None:
        self.spi.xfer2([reg | 0x80, value & 0xFF])

    def _read_burst(self, reg: int, length: int) -> bytes:
        resp = self.spi.xfer2([reg & 0x7F] + [0x00] * length)
        return bytes(resp[1:])

    def _write_burst(self, reg: int, data: bytes) -> None:
        self.spi.xfer2([reg | 0x80] + list(data))

    def _set_mode(self, mode: int) -> None:
        self._write_reg(self.REG_OP_MODE, self.MODE_LONG_RANGE | mode)
        time.sleep(0.005)

    def _set_frequency(self, freq_hz: int) -> None:
        frf = int((freq_hz * (1 << 19)) / 32000000)
        self._write_reg(self.REG_FRF_MSB, (frf >> 16) & 0xFF)
        self._write_reg(self.REG_FRF_MID, (frf >> 8) & 0xFF)
        self._write_reg(self.REG_FRF_LSB, frf & 0xFF)

    def _set_tx_power(self, power_dbm: int) -> None:
        clamped = max(2, min(power_dbm, 17))
        self._write_reg(self.REG_PA_CONFIG, 0x80 | (clamped - 2))

    def initialize(self) -> bool:
        self.spi = spidev.SpiDev()
        self.spi.open(self.spi_bus, self.spi_cs)
        self.spi.max_speed_hz = self.spi_hz
        self.spi.mode = 0

        self.gpio.initialize()
        self.gpio.pulse_reset()

        version = self._read_reg(self.REG_VERSION)
        if version != 0x12:
            raise RuntimeError(f"unexpected SX1276 version register: 0x{version:02X}")

        with self.lock:
            self._set_mode(self.MODE_SLEEP)
            self._write_reg(self.REG_FIFO_TX_BASE_ADDR, 0x80)
            self._write_reg(self.REG_FIFO_RX_BASE_ADDR, 0x00)
            self._set_frequency(self.freq_hz)
            self._set_tx_power(self.tx_power)
            self._write_reg(self.REG_LNA, self._read_reg(self.REG_LNA) | 0x03)
            self._write_reg(self.REG_MODEM_CONFIG1, 0x92)  # BW500k, CR4/5, explicit header
            self._write_reg(self.REG_MODEM_CONFIG2, 0x74)  # SF7, CRC on
            self._write_reg(self.REG_MODEM_CONFIG3, 0x04)  # AGC on
            self._write_reg(self.REG_PREAMBLE_MSB, 0x00)
            self._write_reg(self.REG_PREAMBLE_LSB, 0x08)
            self._write_reg(self.REG_SYNC_WORD, self.sync_word)
            self._write_reg(self.REG_IRQ_FLAGS, 0xFF)
            self._write_reg(self.REG_FIFO_ADDR_PTR, 0x00)
            self._write_reg(self.REG_DIO_MAPPING1, 0x00)  # DIO0=RxDone
            self._set_mode(self.MODE_RX_CONTINUOUS)

        self.ready = True
        return True

    def set_mode_rx(self) -> None:
        with self.lock:
            self._write_reg(self.REG_IRQ_FLAGS, 0xFF)
            self._write_reg(self.REG_DIO_MAPPING1, 0x00)
            self._set_mode(self.MODE_RX_CONTINUOUS)

    def send(self, data: bytes, timeout_s: float = 3.0) -> bool:
        if not self.ready:
            return False

        with self.lock:
            tx_base = self._read_reg(self.REG_FIFO_TX_BASE_ADDR)
            self._set_mode(self.MODE_STDBY)
            self._write_reg(self.REG_IRQ_FLAGS, 0xFF)
            self._write_reg(self.REG_DIO_MAPPING1, 0x40)  # DIO0=TxDone
            self._write_reg(self.REG_FIFO_ADDR_PTR, tx_base)
            self._write_burst(self.REG_FIFO, data)
            self._write_reg(self.REG_PAYLOAD_LENGTH, len(data))
            self._set_mode(self.MODE_TX)

        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            with self.lock:
                irq_flags = self._read_reg(self.REG_IRQ_FLAGS)
                if irq_flags & self.IRQ_TX_DONE:
                    self._write_reg(self.REG_IRQ_FLAGS, 0xFF)
                    self._write_reg(self.REG_FIFO_ADDR_PTR, self._read_reg(self.REG_FIFO_RX_BASE_ADDR))
                    self._write_reg(self.REG_DIO_MAPPING1, 0x00)
                    self._set_mode(self.MODE_RX_CONTINUOUS)
                    return True
            time.sleep(0.01)

        self.set_mode_rx()
        return False

    def receive_once(self) -> Optional[Tuple[bytes, int]]:
        if not self.ready:
            return None

        with self.lock:
            irq_flags = self._read_reg(self.REG_IRQ_FLAGS)
            if (irq_flags & self.IRQ_RX_DONE) == 0:
                return None

            if (irq_flags & self.IRQ_PAYLOAD_CRC_ERROR) != 0:
                self._write_reg(self.REG_IRQ_FLAGS, 0xFF)
                return None

            length = self._read_reg(self.REG_RX_NB_BYTES)
            current_addr = self._read_reg(self.REG_FIFO_RX_CURRENT_ADDR)
            self._write_reg(self.REG_FIFO_ADDR_PTR, current_addr)
            payload = self._read_burst(self.REG_FIFO, length)
            rssi_raw = self._read_reg(self.REG_PKT_RSSI_VALUE)
            rssi_dbm = int(rssi_raw) - 157
            self._write_reg(self.REG_IRQ_FLAGS, 0xFF)
            self._write_reg(self.REG_FIFO_ADDR_PTR, self._read_reg(self.REG_FIFO_RX_BASE_ADDR))
            return payload, rssi_dbm


class LoRaHardware:
    def __init__(self):
        self.on_receive_callback = None
        self.radio: Any = None
        self.last_error: Optional[str] = None
        self.driver_name = "none"
        self._rx_thread: Optional[threading.Thread] = None
        self._rx_stop = threading.Event()

    def is_ready(self) -> bool:
        return self.radio is not None

    def get_last_error(self) -> Optional[str]:
        return self.last_error

    def get_driver_name(self) -> str:
        return self.driver_name

    def _start_builtin_rx_loop(self) -> None:
        if self.driver_name != "builtin-sx1276":
            return
        if self._rx_thread is not None and self._rx_thread.is_alive():
            return

        self._rx_stop.clear()

        def _loop():
            while not self._rx_stop.is_set():
                try:
                    if self.on_receive_callback and self.radio is not None:
                        result = self.radio.receive_once()
                        if result is not None:
                            payload, rssi_dbm = result
                            print(
                                f"[HARDWARE LoRa RX] Received raw frame len={len(payload)} RSSI={rssi_dbm} dBm"
                            )
                            self.on_receive_callback(payload, rssi_dbm)
                except Exception as e:
                    self.last_error = str(e)
                    print(f"[HARDWARE LoRa RX] ERROR: {e}")
                time.sleep(self.radio.poll_interval if self.radio is not None else 0.05)

        self._rx_thread = threading.Thread(target=_loop, name="laviet-lora-rx", daemon=True)
        self._rx_thread.start()

    def initialize(self):
        print("[HARDWARE LoRa] Initializing radio hardware...")
        self.last_error = None

        def internal_callback(data, rssi=None, index=None):
            if self.on_receive_callback and data:
                raw_bytes = bytes([ord(c) for c in data])
                rssi_val = rssi if rssi is not None else 0
                print(
                    f"[HARDWARE LoRa RX] Received raw frame len={len(raw_bytes)} RSSI={rssi_val} dBm"
                )
                self.on_receive_callback(raw_bytes, rssi_val)

        if RadioHandler is not None and RadioMode is not None:
            try:
                self.radio = RadioHandler(RadioMode.LORA, internal_callback)
                self.radio.lora_handler._spi_write(0x39, 0x34)
                op_mode = self.radio.lora_handler._spi_read(0x01)
                sync_word = self.radio.lora_handler._spi_read(0x39)
                print(
                    f"[HARDWARE LoRa] Using external radio_handle.py OP_MODE=0x{op_mode:02X} "
                    f"SYNC_WORD=0x{sync_word:02X}"
                )
                self.driver_name = "radio_handle"
                return True
            except Exception as e:
                self.radio = None
                self.last_error = str(e)
                print(f"[HARDWARE LoRa] External driver init failed: {e}")

        try:
            self.radio = _SX1276LoRaRadio()
            self.radio.initialize()
            self.driver_name = "builtin-sx1276"
            print(
                f"[HARDWARE LoRa] Builtin SX1276 driver ready "
                f"(spi={self.radio.spi_bus}.{self.radio.spi_cs}, reset_gpio={self.radio.reset_pin}, "
                f"dio0_gpio={self.radio.dio0_pin}, freq={self.radio.freq_hz}, "
                f"sync=0x{self.radio.sync_word:02X})"
            )
            return True
        except Exception as e:
            self.radio = None
            self.driver_name = "none"
            self.last_error = str(e)
            print(f"[HARDWARE LoRa ERROR] Failed to initialize radio: {e}")
            return False

    def send_frame(self, frame: bytes) -> bool:
        if not self.radio:
            self.last_error = "radio is not initialized"
            print("[HARDWARE LoRa TX] ERROR: refusing TX because radio is not initialized")
            return False

        print(f"[HARDWARE LoRa TX] len={len(frame)} hex={frame.hex()}")

        if self.driver_name == "radio_handle":
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

        try:
            tx_ok = self.radio.send(frame)
            if not tx_ok:
                self.last_error = "TX timeout waiting for TxDone"
                print("[HARDWARE LoRa TX] ERROR: TX timeout waiting for TxDone")
                return False

            self.last_error = None
            print(f"[HARDWARE LoRa TX] OK ({len(frame)} B)")
            return True
        except Exception as e:
            self.last_error = str(e)
            print(f"[HARDWARE LoRa TX] ERROR: {e}")
            try:
                self.radio.set_mode_rx()
            except Exception:
                pass
            return False

    def attach_receive_interrupt(self, callback: Callable[[bytes, int], None]):
        self.on_receive_callback = callback
        print("[HARDWARE LoRa] RX callback attached")
        self._start_builtin_rx_loop()


lora_device = LoRaHardware()
