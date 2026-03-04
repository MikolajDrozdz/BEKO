# Core/App - FreeRTOS Application Layer

Aktualna architektura jest oparta o zadania RTOS i Active Object (kolejki komend/zdarzeń):

- `button_main` - polling 3 przycisków co 25 ms (`DIG_B1/B2/B3`), debounce, short/long press.
- `menu_main` - kontroler UI na LCD 20x4 (pager/menu/popup), auto-exit 60 s.
- `lcd_main` - dedykowany task LCD z trybami `MONITOR/MENU/POPUP`, scroll wiadomości RX.
- `radio_main` - task radiowy z protokołem `BEKO_NET_V1`, TTL+dedup forwarding, pairing, presety LoRa.
- `security_main` - task domeny security: runtime config, rotacja klucza, TPM bootstrap, log RX do EEPROM.
- `bmp280_main`, `tof_main`, `led_array_main` - zadania sprzętowe jak wcześniej.

`app.c` pozostaje orchestratoriem: inicjuje współdzielony lock I2C i uruchamia taski.

## Startup

1. `main.c` inicjalizuje HAL/peryferia.
2. `app_init()`.
3. `osKernelInitialize()`.
4. `MX_FREERTOS_Init()` -> `app_freertos_init()` tworzy taski.
5. `osKernelStart()`.

LCD pokazuje animację `HELLO BEKO`, następnie przechodzi do monitora RX.

## Routing i UI

- Ramki systemowe: `BEKO_NET_V1` (`BK`, ver=1, ttl, src/dst, msg_id, payload, crc16).
- `node_id` wyliczane z UID MCU.
- Forwarding: tylko ramki systemowe nie-do-mnie, `ttl > 1`, bez duplikatów (cache 32/60 s).
- Każda wiadomość RX:
  - print na UART,
  - jedna linia LCD (`RSSI:payload`, 20 znaków),
  - log do EEPROM (przez `security_main`).

## Build note

Jeśli IDE nie podchwyci nowych plików automatycznie, zrób:

1. Refresh projektu
2. Clean
3. Rebuild

