# TODO - BEKO Pager Network (`pager-rtos`)

Ten plik zastepuje starsze TODO zwiazane z mesh i dawna dokumentacja `BEKO_NET_V1`.
Aktualny stan repo dotyczy architektury:
- `1 Raspberry Pi Gateway + nody STM32`
- topologia gwiazdy
- ramki `LAVIET_FRAME_V1`
- `AES-CTR` + `HMAC-SHA256`
- obowiazkowy `ACK` dla unicastu
- model `TPM-first`

Data aktualizacji: `2026-04-19`

---

## 1. Stan wdrozony

- [x] Finalny format `LAVIET_FRAME_V1`
- [x] Walidacja ramek, `payload_len`, typow i flag
- [x] Obsluga `DATA`, `ACK`, `RESP`, `PAIR_REQ`, `PAIR_RESP`, `CFG`, `COUNTER_SYNC`, `KEY_ROTATE`, `ERROR`
- [x] Limit `payload` do `0..16 B`
- [x] `AES-CTR` dla `payload`
- [x] `HMAC-SHA256` dla chronionej czesci ramki
- [x] `ACK` dla poprawnych ramek unicast
- [x] Timeout `ACK` i retransmisje po stronie noda
- [x] Rozroznienie stanow `ACK`: poprawny / spozniony / zduplikowany / nieoczekiwany / timeout
- [x] Pairing sieciowy: gateway inicjuje, node nasluchuje
- [x] Osobny PIN uzytkownika i administratora
- [x] Dokumentacja kontraktu w `gateway_docs/`
- [x] Gateway backend w Pythonie (`gateway/`) z FastAPI + SQLite + LoRa SX1276

---

## 2. Protokol i ramki

- [x] Parser i serializer zgodne z `LAVIET_FRAME_V1`
- [x] Big-endian dla pol wielobajtowych
- [x] Walidacja `src_id`, `dst_id`, broadcastu i typow ramek
- [x] Broadcast usuniety z normalnej sciezki UI uzytkownika
- [x] `PAIR_REQ` sieciowy tylko od gatewaya

### Uwagi

- Broadcast nadal istnieje na poziomie protokolu.
- UI noda nie daje juz uzytkownikowi zwyklej sciezki do wysylania broadcastu.

---

## 3. ACK i niezawodnosc

- [x] `ACK` dla poprawnego unicastu z `ACK_REQUIRED`
- [x] Tracker oczekujacego `ACK` po stronie noda
- [x] Retry i timeout `ACK` po stronie noda
- [x] Mapowanie `ACK -> msg_id + counter`
- [x] Gateway backend zapisuje i rozlicza `ACK` dla wyslanych wiadomosci

### Otwarte

- [ ] Dodac bardziej rozbudowany scheduler retry / kolejke retransmisji po stronie gatewaya
- [ ] Dodac wyzszy poziom raportowania statusu dostarczenia w panelu operatorskim

---

## 4. AES-CTR

- [x] `AES-CTR` w firmware STM32
- [x] Jednoznaczny `counter block` / nonce
- [x] Zerowanie buforow z plaintextem i kluczami
- [x] Runtime self-test hardware AES wzgledem referencji software

### Otwarte

- [ ] Dodac osobne testy regresyjne AES poza self-testem runtime

---

## 5. HMAC-SHA256

- [x] `HMAC-SHA256` w firmware STM32
- [x] `HMAC-SHA256` w gateway backendzie
- [x] `HMAC` liczony po `header || payload`
- [x] Dla `ENCRYPTED=1` HMAC liczony po ciphertext
- [x] RX: najpierw HMAC, potem decrypt
- [x] Staly testowy MAC `01 x 32` usuniety z aktywnej sciezki
- [x] Dodane szczegolowe logi debug HMAC po obu stronach

### Otwarte

- [ ] Potwierdzic end-to-end po swiezym pairingu, ze direct `node -> gateway` i `gateway -> node` dzialaja na finalnym HMAC bez sciezek debug
- [ ] Dodac zewnetrzne testy regresyjne / wektory HMAC poza firmware

---

## 6. Pairing i trusted devices

- [x] Finalny przebieg `PAIR_REQ` / `PAIR_RESP`
- [x] Local consent na nodzie
- [x] Zapis trusted devices w pamieci nieulotnej
- [x] Slot `0` rezerwowany dla gatewaya (`G`)
- [x] Usuwanie duplikatow gatewaya z innych slotow
- [x] Gateway backend zapisuje `paired_code` od razu po `PAIR_RESP`
- [x] Gateway backend sprawdza, ze `PAIR_RESP` zawiera dokladnie ten sam `code[8]`, ktory wyslal w `PAIR_REQ`
- [x] Poprawka STM32: trusted-device code zwiekszony z `6 B` do `8 B`

### Otwarte

- [ ] Po kazdej zmianie formatu trusted storage wykonywac test migracji / re-pairingu
- [ ] Przetestowac i domknac polityke RSSI dla pairingu sieciowego

### Uwaga krytyczna

- Stare wpisy trusted zapisane przed poprawka `8 B code` moga byc niezgodne z aktualnym KDF.
- Po tej zmianie wymagany jest swiezy re-pairing urzadzen.

---

## 7. Gateway Raspberry Pi

- [x] FastAPI + SQLite + modele `Node`, `Message`, `Log`
- [x] Endpointy `messages`, `pairing`, `nodes`, `system`, `logs`
- [x] Background listener LoRa
- [x] Realny backend SX1276/RFM95 przez SPI na RPi
- [x] Realne TX/RX zamiast symulacji
- [x] Weryfikacja `PAIR_RESP` i zapisu kodu pairingu
- [x] Debug HMAC dla TX i RX

### Otwarte

- [ ] Dodac pelniejszy panel operatorski / UI
- [ ] Dodac twardsze recovery po bledach radia i watchdog dla backendu
- [ ] Dopiac bardziej szczegolowa konfiguracje deploymentu na RPi

---

## 8. TPM-first i zarzadzanie sekretami

- [x] `TPM-first` bootstrap w `security_main`
- [x] Fallback do entropii lokalnej, gdy TPM backend nie dostarcza danych
- [x] Usuniecie jawnych testowych kluczy z glownej sciezki roboczej

### Otwarte

- [ ] Dokonczyc trwala persystencje root seeda w backendzie TPM NV
- [ ] Dodac testy odtwarzania sekretow po reboocie
- [ ] Udokumentowac finalny model lifecycle kluczy

---

## 9. Anti-replay i counters

- [x] Liczniki per relacja gateway-node
- [x] `ACK` odnosi sie do `msg_id + counter`
- [x] Counter gatewaya jest zapisywany i odtwarzany

### Otwarte

- [ ] Domknac rollback-safe storage countera przy zaniku zasilania
- [ ] Przywrocic finalna polityke anti-replay po zakonczeniu diagnostyki HMAC/direct
- [ ] Dodac testy replay attack i restart/recovery

---

## 10. Rotacja kluczy

- [ ] Wybrac finalny mechanizm rotacji (`DH` vs `ECDH` lub inny)
- [ ] Zaimplementowac `KEY_ROTATE`
- [ ] Dodac derivation nowych kluczy
- [ ] Dodac bezpieczne przelaczanie starych/nowych kluczy
- [ ] Dodac logi i testy rotacji

---

## 11. Warstwa radiowa

- [x] Radio pozostaje `SX1276/RFM95`
- [x] Konfiguracja zgodna z aktualnym firmware (`868.5 MHz`, `BW500k`, `SF7`, `CR4/5`, `sync=0x34`)
- [x] Gateway ma realny backend sprzetowy zamiast samego mocka

### Otwarte

- [ ] Potwierdzic zachowanie przy slabszym sygnale i zakloceniach
- [ ] Dodac bardziej rozbudowane logi timeoutow / bledow radiowych po stronie gatewaya
- [ ] Dodac testy dlugiej pracy i recovery po bledach SPI / IRQ

---

## 12. Role i kontrola dostepu

- [x] Rozdzielenie PIN user/admin
- [x] Osobny PIN do menu administratora

### Otwarte

- [ ] Pelny model rol `administrator / operator / uzytkownik / serwisant`
- [ ] Polityki uprawnien dla menu i operacji radiowych
- [ ] Logowanie dzialan uprzywilejowanych

---

## 13. Diagnostyka i startup

- [x] Rozbudowane logi UART dla RX/TX
- [x] Rozbudowane logi HMAC i kluczy diagnostycznych
- [x] Gateway loguje szczegoly TX/RX i walidacji MAC

### Otwarte

- [ ] Dodac pelny raport startupu z czasami init
- [ ] Rozdzielic finalnie logi debug vs production
- [ ] Dodac kontrole zgodnosci firmware/config przy starcie

---

## 14. Testy i walidacja koncowa

- [ ] Pelny test end-to-end: `PAIR_REQ -> PAIR_RESP -> direct DATA -> ACK`
- [ ] Test `gateway -> node` plaintext i ciphertext
- [ ] Test `node -> gateway` plaintext i ciphertext
- [ ] Test replay / reset / restore countera
- [ ] Test zaniku zasilania podczas zapisu storage
- [ ] Test pracy na 2 urzadzeniach i z gatewayem RPi przez dluzszy czas
- [ ] Testy regresyjne parsera / frame / crypto poza runtime self-testami

---

## 15. Najblizsze priorytety

1. Potwierdzic po swiezym re-pairingu finalne direct `node <-> gateway` na prawdziwym `PAIR_V1_32 + HMAC`.
2. Domknac persystencje TPM NV dla root seeda.
3. Domknac rollback-safe counter storage.
4. Zaimplementowac `KEY_ROTATE`.
5. Zrobic pelne testy end-to-end i walidacje radiowa.

---

## Zarchiwizowane / nieaktualne zalozenia

Te punkty nie sa juz celem aktywnego wdrozenia:

- mesh / relay / forwarding
- TTL w ramce aplikacyjnej
- stare `BEKO_NET_V1`
- `AuthTag 4 B`
- `XTEA-CTR`
- stare TODO zakladajace wiele relay-node'ow
