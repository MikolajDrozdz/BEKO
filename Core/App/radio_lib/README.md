# radio_lib

Modułowa biblioteka radiowa dla układów SX1276/RFM95 (np. RFM95W-862S2) dla STM32.
Warstwa aplikacyjna korzysta z jednego API (`radio_lib.h`), a konkretna modulacja
jest wybierana kompilacyjnie.

## Cele projektu

- Stabilne API do inicjalizacji, TX/RX i obsługi zdarzeń IRQ.
- Rozdzielenie warstwy sprzętowej (rejestry SX1276) od logiki modulacji.
- Możliwość rozbudowy o kolejne backendy bez zmian w aplikacji.
- Utrzymanie zgodności z kodem generowanym przez CubeMX.

## Struktura katalogu

| Ścieżka | Rola |
|---|---|
| `radio_lib.h` | Publiczne API biblioteki (statusy, stany, konfiguracje, funkcje). |
| `radio_lib.c` | Fasada/dispatcher delegująca wywołania do aktywnego backendu modulacji. |
| `radio_lib_config.h` | Konfiguracja kompilacyjna: wybór modulacji, timeouty, limity, EXTI. |
| `common/sx1276/radio_sx1276_regs.h` | Definicje rejestrów SX1276 i masek bitowych. |
| `common/sx1276/radio_sx1276.h` | Publiczny interfejs niskopoziomowy (SPI/rejestry/FIFO/IRQ). |
| `common/sx1276/radio_sx1276.c` | Implementacja dostępu do rejestrów SX1276. |
| `modulations/lora/radio_lora.h` | Kontrakt backendu LoRa. |
| `modulations/lora/radio_lora.c` | Implementacja LoRa: init, TX/RX, zdarzenia, DIO/IRQ, recovery. |
| `modulations/fsk/radio_fsk.h` | Kontrakt backendu FSK. |
| `modulations/fsk/radio_fsk.c` | Szkielet backendu FSK (placeholder, `RADIO_ESTATE`). |
| `modulations/ook/radio_ook.h` | Kontrakt backendu OOK. |
| `modulations/ook/radio_ook.c` | Szkielet backendu OOK (placeholder, `RADIO_ESTATE`). |
| `test/radio_test.h` | Publiczne narzędzia testowe i diagnostyczne. |
| `test/radio_test.c` | Scenariusz demo: probe, dump rejestrów, okresowy `PING`, logowanie zdarzeń. |

## Wybór aktywnej modulacji

W pliku `radio_lib_config.h` ustaw makro:

- `RADIO_LIB_ACTIVE_MODULATION = RADIO_LIB_MODULATION_LORA` (domyślnie),
- `RADIO_LIB_ACTIVE_MODULATION = RADIO_LIB_MODULATION_FSK`,
- `RADIO_LIB_ACTIVE_MODULATION = RADIO_LIB_MODULATION_OOK`.

Aplikacja zawsze używa `radio_lib.h`; zmienia się wyłącznie backend wewnętrzny.

## Model obsługi przerwań (IRQ/EXTI)

- EXTI pozostaje po stronie kodu CubeMX (`HAL_GPIO_EXTI_IRQHandler(...)`).
- `HAL_GPIO_EXTI_Callback` może być implementowany przez bibliotekę
  (`RADIO_LIB_OWNS_HAL_EXTI_CALLBACK = 1`) albo przez aplikację (`= 0`)
  z przekazaniem pinu do `radio_on_exti(...)`.
- ISR ustawia tylko lekkie flagi; operacje SPI i logika pakietów są wykonywane w `radio_process()`.

## Integracja w aplikacji

Minimalny przepływ:

1. `radio_default_hw_cfg(...)` i `radio_default_lora_cfg(...)`.
2. `radio_init(...)`.
3. `radio_start_rx_continuous()` lub `radio_start_rx_single(...)`.
4. W pętli głównej: `radio_process()` oraz `radio_take_events()`.

Do szybkiego uruchomienia można użyć `test/radio_test.c`:

- `radio_test_demo_init(&hspi1)`,
- `radio_test_demo_process()` cyklicznie w pętli głównej.

## Status backendów

- LoRa: implementacja produkcyjna (SX1276/RFM95).
- FSK: szkielet API przygotowany do dalszej implementacji.
- OOK: szkielet API przygotowany do dalszej implementacji.

## Dokumentacja Doxygen

Pliki `.h` zawierają pełną dokumentację publicznego API w stylu Doxygen.
Pliki `.c` zawierają dokumentację modułów oraz sekcji implementacyjnych.

Przykładowe znaczniki użyte w projekcie:

- `@file`, `@brief`,
- `@param`, `@return`,
- `@name` i `@{ ... @}` dla grupowania API.
