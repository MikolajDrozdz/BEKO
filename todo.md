# Plan Implementacji: Kontroler Menu + Button Task + Security/Forwarding (RTOS, LCD 20x4)

## Summary
Zaimplementujemy pełny kontroler UI oparty o taski RTOS: `button_main` (polling co 25 ms), `menu_main` (stanowa nawigacja i rendering), `security_main` (TPM + polityki security), oraz rozszerzony `radio_main` (routing, forwarding, konfiguracja modulacji, pairing).  
Start systemu: animacja `HELLO BEKO`, potem tryb domyślny RX na ustawieniach demo, ale bez auto-PING.  
Po naciśnięciu dowolnego przycisku: wejście do `pager_menu`.  
Dodamy forwarding wiadomości systemowych (TTL + dedup), menu hierarchiczne zgodne z wymaganiami, oraz trwałość ustawień.

## Decyzje Zablokowane (z rozmowy)
1. Nawigacja: 3 przyciski (`UP`, `DOWN`, `OK`), back = długi `OK`.
2. Piny przycisków: `dig1=UP=PC8`, `dig2=DOWN=PA2`, `dig3=OK=PA3`, aktywne stanem niskim.
3. Polling przycisków: co 25 ms, bez EXTI.
4. Zakres: maksymalny (pełny kontroler + domena security + routing).
5. Forwarding: tak, `TTL + dedup`.
6. Auto-PING demo: wyłączony domyślnie.
7. Pairing: `Add new device` = 60 s, pierwszy `JOIN_REQ`, potem akceptacja.
8. Trusted devices: TPM NV, `policy session`, layout per-device, limit 16 urządzeń.
9. Policy: `PCR + PP + CommandCode`.
10. PP: oddzielny sygnał fizyczny związany z TPM (nie używamy MCU GPIO do PP).
11. Coding radiowe: `XTEA-CTR + CRC16`, klucz z TPM NV.
12. Frequency hopping: profil `868.1/868.3/868.5 MHz`, hop co 2 s, stan przywracany z pamięci.
13. LoRa w menu: presety.
14. FSK/OOK: pozycje dostępne w menu, próba aktywacji kończy się kontrolowanym błędem runtime.
15. Powiadomienia podczas menu: tryb konfigurowalny, domyślnie `Popup`, zamknięcie dowolnym przyciskiem.
16. Integracja GPIO: przez aktualizację `.ioc` (nie tylko USER CODE).
17. Szablony wiadomości: grupy `ALERT/STATUS/SERVICE`.

## Architektura Tasków i Odpowiedzialność
1. `button_main` (nowy task): polling GPIO co 25 ms, debouncing, short/long press, publikacja eventów.
2. `menu_main` (nowy task): state machine UI, `pager_menu`, menu główne, podmenu i operacje użytkownika.
3. `security_main` (nowy task): operacje TPM (NV, policy session), klucze, trusted devices, funkcje security.
4. `radio_main` (refactor): AO z kolejką komend; RX/TX, parse ramek systemowych, relay, dedup, hop, presety LoRa.
5. `lcd_main` (istniejący AO): rozszerzenie o tryby renderu `monitor/menu/popup`, arbitraż źródeł wyświetlania.
6. Istniejące taski `bmp280_main`, `tof_main`, `led_array_main`: pozostają, menu wywołuje ich API.

## Zmiany w Interfejsach Publicznych (API/typy)
1. `button_main.h` (nowy):
- `void button_main_create_task(void);`
- `bool button_main_get_event(button_event_t *evt, uint32_t timeout_ms);`
- `button_event_t`: `UP_SHORT`, `DOWN_SHORT`, `OK_SHORT`, `OK_LONG`.

2. `menu_main.h` (nowy):
- `void menu_main_create_task(void);`
- `bool menu_main_post_notification(const menu_notification_t *n);`
- `menu_notification_t`: RX, warning, error, pairing prompt, security result.

3. `security_main.h` (nowy):
- `void security_main_create_task(void);`
- `bool security_main_cmd_add_device(uint32_t node_id, const uint8_t *code, uint8_t len);`
- `bool security_main_cmd_delete_device(uint32_t node_id);`
- `bool security_main_cmd_get_device(uint8_t idx, trusted_info_t *out);`
- `bool security_main_cmd_set_coding(bool enabled);`
- `bool security_main_cmd_rotate_key(void);`
- `bool security_main_cmd_set_fh(bool enabled);`

4. `radio_main.h` (rozszerzenie):
- `bool radio_main_cmd_send_template(uint8_t group_id, uint8_t msg_id, uint32_t dst_id);`
- `bool radio_main_cmd_set_lora_preset(uint8_t preset_id);`
- `bool radio_main_cmd_set_modulation(uint8_t modulation_id);`
- `bool radio_main_cmd_set_fh(bool enabled);`
- `bool radio_main_cmd_set_coding(bool enabled);`
- `bool radio_main_cmd_set_auto_ping(bool enabled);`

5. `lcd_main.h` (rozszerzenie):
- `bool lcd_main_set_mode(lcd_main_mode_t mode);`
- `bool lcd_main_show_popup(const char *l0, const char *l1, const char *l2, const char *l3);`
- `bool lcd_main_show_boot_hello(void);`
- `lcd_main_mode_t`: `LCD_MODE_MONITOR`, `LCD_MODE_MENU`, `LCD_MODE_POPUP`.

6. `st33ktpm2x.h/.c` (rozszerzenie TPM):
- nowe helpery sesji/policy/NV: `start_auth_session`, `policy_pcr`, `policy_physical_presence`, `policy_command_code`, `policy_or`, `nv_define`, `nv_read`, `nv_write`, `flush_context`.
- struktury rekordów trusted i storage key.

## Format Ramek Systemowych i Routing
1. Protokół `BEKO_NET_V1` (binarny):
- `magic(2)='BK'`, `ver(1)=1`, `type(1)`, `flags(1)`, `ttl(1)`, `src_id(4)`, `dst_id(4)`, `msg_id(4)`, `payload_len(2)`, `payload(N)`, `crc16(2)`.
2. Domyślne `node_id`: hash z `HAL_GetUIDw0/1/2`.
3. `TTL` domyślne: 3.
4. Dedup cache: 32 wpisy, okno 60 s.
5. Forward rule:
- forward tylko jeśli ramka jest systemowa i nie jest do tego urządzenia.
- decrement TTL i relay gdy `ttl > 1`.
- brak relay dla duplikatu (dedup hit) i własnych ramek (`src_id == self`).
6. ACK policy:
- ACK tylko dla pairing (`JOIN_REQ/JOIN_ACCEPT` flow).
7. Wiadomości niesystemowe:
- lokalny print/LCD/log, bez relay.

## Security i TPM NV (Policy Session)
1. Klucz sieci (16 B XTEA) w TPM NV, handle dedykowany.
2. Trusted devices: `per-device NV index`, 16 slotów.
3. Metadata trusted (maska zajętości + wersja) w osobnym NV index.
4. Policy:
- branch R: `PolicyPCR + PolicyPhysicalPresence + PolicyCommandCode(NV_Read)`.
- branch W: `PolicyPCR + PolicyPhysicalPresence + PolicyCommandCode(NV_Write)`.
- final auth przez `PolicyOR` branchy.
5. PP:
- traktowany jako sygnał fizyczny TPM; menu pokaże instrukcję „naciśnij PP” i retry sesji do timeout.
6. Security->Keys:
- rotate key: nowy klucz z TPM RNG, zapis TPM NV, potwierdzenie w UI.
7. Security->Coding:
- ON/OFF kodowania radiowego (`XTEA-CTR + CRC16`), stan trwały.
8. Security->Frequency hopping:
- ON/OFF + profil kanałów, stan trwały.

## Menu i UX (LCD 20x4)
1. Boot:
- `HELLO BEKO` animacja z istniejącej biblioteki.
- automatyczne przejście do monitor RX.
2. Monitor RX:
- linie przewijane jak terminal, format `RSSI4:MSG` (20 kolumn, truncation).
3. Wejście do menu:
- dowolny przycisk z monitora otwiera `pager_menu`.
4. `pager_menu`:
- `Send message`
- `Message groups`
- `Main menu`
- `Exit`
5. `Main menu`:
- `Devices` -> `Add new device`, `Delete device`, `Info device`.
- `Security` -> `Frequency hopping`, `TPM`, `Keys`, `Coding`.
- `Hardware` -> `Dist measure/Measure`, `Temperature`, `Pressure`, `Led`.
- `Modulation` -> `LoRa`, `FSK`, `OOK`.
- `Info`.
6. Nawigacja:
- `UP/DOWN`: zmiana pozycji.
- `OK short`: enter/select.
- `OK long`: back/cancel.
7. Powiadomienia:
- tryb konfigurowalny (`Popup` lub `Badge`), domyślnie `Popup`.
- popup zamykany dowolnym przyciskiem.
8. Auto-exit:
- 60 s bezczynności -> powrót do monitor RX.

## Presety i Funkcje Domenowe
1. LoRa presety:
- `STD`: 868.1 MHz, BW125, SF7, CR4/5.
- `RANGE`: 868.3 MHz, BW125, SF12, CR4/5.
- `FAST`: 868.5 MHz, BW500, SF7, CR4/5.
2. FSK/OOK:
- wybór dozwolony, runtime zwraca błąd i pokazuje status (bez crash/restart).
3. Szablony wiadomości:
- `ALERT`: `ALR:FIRE`, `ALR:INTRUSION`, `ALR:LOWBATT`.
- `STATUS`: `STS:OK`, `STS:BUSY`, `STS:IDLE`.
- `SERVICE`: `SRV:PING`, `SRV:RESET`, `SRV:SYNC`.

## Trwałość Ustawień
1. Ustawienia UI/radia (FH state, notification mode, coding state, ostatni preset LoRa, auto-ping) trzymane w EEPROM secret slot (`i2c_mem_store_secret_*`).
2. Trusted devices i key material trzymane w TPM NV.
3. Domyślne boot fallback:
- auto-ping OFF,
- coding ON/OFF wg zapisu,
- notify `Popup`,
- FH stan przywrócony z pamięci.

## Zmiany w Plikach (Plan)
1. Nowe pliki:
- `Core/App/button_main.c`, `Core/App/button_main.h`
- `Core/App/menu_main.c`, `Core/App/menu_main.h`
- `Core/App/security_main.c`, `Core/App/security_main.h`
- `Core/App/beko_net_proto.c`, `Core/App/beko_net_proto.h`
2. Modyfikacje:
- `Core/App/app.c` (rejestracja nowych tasków)
- `Core/App/radio_main.c/.h` (AO + routing/forwarding/komendy)
- `Core/App/lcd_main.c/.h` (tryby monitor/menu/popup/boot)
- `Core/App/st33ktpm2x_lib/st33ktpm2x.c/.h` (policy + NV API)
- `Core/App/radio_lib/*` (reconfigure LoRa runtime, obsługa błędów modulacji)
- `Core/App/radio_lib/test/radio_test.c` (redukcja do diagnostyki, bez sterowania runtime)
- `BEKO_W1_hello-.ioc`, `Core/Src/main.c`, `Core/Inc/main.h` (PC8/PA2/PA3 GPIO input + etykiety)
- dokumentacja: `README.md`, `Core/App/README.md`, README modułów.
3. Nazewnictwo:
- używamy `menu_main` (zgodnie ze stylem projektu), alias `manu_main` nie będzie dodawany.

## Test Cases i Kryteria Akceptacji
1. Boot/UI:
- Po starcie widać `HELLO BEKO`, potem monitor RX.
- Dowolny klawisz otwiera `pager_menu`.
2. Button timing:
- Polling dokładnie co 25 ms (log timestamp + brak EXTI zależności).
- Short/long press działa deterministycznie.
3. RX/LCD/UART:
- każda odebrana wiadomość: print na UART + jedna linia LCD, trunc do 20 znaków, scroll terminalowy.
4. Menu logic:
- pełna nawigacja drzewem, `OK long` wraca poziom wyżej, auto-exit 60 s.
5. Devices pairing:
- tryb 60 s, pierwszy JOIN_REQ, akceptacja/reject, wynik na LCD/UART.
6. Security TPM:
- odczyt TPM info działa.
- read/write trusted list przez NV policy session działa przy aktywnym PP.
- keys rotate zapisuje nowy key w TPM NV.
7. Routing:
- forwarding działa dla ramek systemowych obcych, TTL maleje.
- dedup 32/60s blokuje duplikaty.
- wiadomości niesystemowe nie są forwardowane.
8. Modulation:
- LoRa presety przełączalne i skuteczne.
- FSK/OOK wybieralne, kończą się kontrolowanym błędem runtime.
9. Persistence:
- restart przywraca FH state, notify mode, coding state i preset LoRa.
10. Regressions:
- brak degradacji odczytu BMP280/ToF i LED task.

## Assumptions i Domyślne Założenia
1. `PP` jest realizowany sprzętowo po stronie TPM (nie wymagamy dedykowanego GPIO MCU `gpio_pp`).
2. Piny `PC8/PA2/PA3` są fizycznie podłączone do przycisków i wolne logicznie.
3. FSK/OOK backend pozostaje placeholderem w tej iteracji; obsłużymy to przez kontrolowany błąd.
4. Dla kodowania radiowego używamy XTEA-CTR (spójność z istniejącym kodem), mimo że produkcyjnie docelowo można rozważyć mocniejszą kryptografię.
5. Aktualizacja `.ioc` i regeneracja kodu CubeMX jest częścią implementacji.
