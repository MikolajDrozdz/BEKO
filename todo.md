# TODO - BEKO Pager Network (`pager-rtos`)

Aktualny kierunek projektu:
- `1 Raspberry Pi Gateway + nody STM32`
- topologia gwiazdy, bez mesh/relay jako celu wdrozenia
- ramki `LAVIET_FRAME_V1`
- adresacja 16-bit: gateway `0x0001`, broadcast `0xFFFF`, node `0x0002..0xFFFE`
- `AES-CTR` + `HMAC-SHA256`
- ACK dla unicastu
- model `TPM-first`
- root seed w TPM NV z zapisem chronionym `PPWRITE`

Data aktualizacji: `2026-04-26`

---

## Stan wdrozony

- [x] Finalny format `LAVIET_FRAME_V1`, parser, serializer i walidacja pol/flag/typow
- [x] Obsluga `DATA`, `ACK`, `RESP`, `PAIR_REQ`, `PAIR_RESP`, `CFG`, `COUNTER_SYNC`, `KEY_ROTATE`, `ERROR`
- [x] Limit payloadu ramki do `16 B`
- [x] `AES-CTR` dla payloadu i `HMAC-SHA256` po `header || payload`
- [x] RX: najpierw HMAC, potem decrypt
- [x] ACK dla unicastu: tracker, timeout, retry, dopasowanie po `msg_id + counter`
- [x] Pairing sieciowy z gatewayem i zapis trusted devices
- [x] Slot `0` zarezerwowany dla gatewaya, node slots `1..15`
- [x] Gateway backend: FastAPI + SQLite + SX1276/RFM95 na RPi
- [x] UI noda: brak zwyklej wysylki broadcast, gateway/broadcast pokazywane nazwami
- [x] PIN operator/admin, PIN settings w Security
- [x] Operator moze czytac i odpowiadac na wiadomosci; menu wymaga swiadomego wejscia i PIN
- [x] Popup RX z szybka odpowiedzia `YES/OK/NO` dla wiadomosci konczacych sie `.`, `?`, `!`
- [x] Monitor LCD pokazuje nadawce i tresc bez RSSI
- [x] TPM init przed EEPROM, root seed w TPM NV `0x01C10101`
- [x] Rotacja lokalnego root seeda wymaga aktywnego pinu TPM PP

---

## Najblizsze priorytety

1. Potwierdzic po swiezym re-pairingu finalne direct `node <-> gateway` na prawdziwym `PAIR_V1_32 + HMAC`.
2. Przetestowac odtworzenie sekretow po reboocie: TPM NV `0x01C10101`, EEPROM key z root seeda i migracje ze starego `0x01C10100`.
3. Domknac rollback-safe storage countera przy zaniku zasilania.
4. Zaimplementowac i przetestowac `KEY_ROTATE`.
5. Zrobic pelny test end-to-end i dluzszy test radiowy na RPi gateway + minimum 2 nody.

---

## Otwarte prace

### Protokol, ACK, crypto

- [ ] Dodac scheduler retry / kolejke retransmisji po stronie gatewaya
- [ ] Dodac zewnetrzne testy regresyjne AES/HMAC/frame poza runtime self-testami
- [ ] Przywrocic finalna polityke anti-replay po zakonczeniu diagnostyki HMAC/direct
- [ ] Dodac test replay attack oraz reset/recovery countera

### Pairing i trusted storage

- [ ] Po zmianach formatu trusted storage wykonywac test migracji / re-pairingu
- [ ] Przetestowac i domknac polityke RSSI dla pairingu sieciowego
- [ ] Pamietac: stare trusted entries sprzed `8 B code` wymagaja swiezego re-pairingu

### Gateway Raspberry Pi

- [ ] Dopiac panel operatorski / UI
- [ ] Dodac twardsze recovery po bledach radia i watchdog backendu
- [ ] Dopiac konfiguracje deploymentu na RPi
- [ ] Raportowac status dostarczenia w panelu operatorskim

### TPM i sekrety

- [x] Trwala persystencja root seeda w TPM NV
- [x] Zapis root seeda chroniony TPM PP (`PPWRITE`, pin TPM 7 aktywny VDD)
- [x] Klucz szyfrowania sekretow EEPROM wyprowadzany z TPM-backed root seeda
- [ ] Dodac test odtwarzania sekretow po reboocie
- [ ] Przetestowac migracje z legacy TPM NV `0x01C10100` do `0x01C10101`
- [x] Udokumentowac aktualny lifecycle kluczy (`Core/App/security/SECURITY_ARCHITECTURE.md`)

### Rotacja kluczy

- [x] Lokalna rotacja root seeda przez menu `Security -> Keys` wymaga TPM PP
- [ ] Wybrac finalny mechanizm rotacji (`DH`, `ECDH` albo inny)
- [ ] Zaimplementowac `KEY_ROTATE`
- [ ] Dodac derivation nowych kluczy
- [ ] Dodac bezpieczne przelaczanie starych/nowych kluczy
- [ ] Dodac logi i testy rotacji

### Radio i diagnostyka

- [ ] Potwierdzic zachowanie przy slabszym sygnale i zakloceniach
- [ ] Dodac testy dlugiej pracy i recovery po bledach SPI / IRQ
- [ ] Rozdzielic finalnie logi debug vs production
- [ ] Dodac raport startupu z czasami init i kontrola zgodnosci firmware/config

### Role i uprawnienia

- [ ] Domknac model rol: administrator / operator / user / serwisant
- [ ] Logowac dzialania uprzywilejowane
- [ ] Doprecyzowac, ktore operacje radiowe sa admin-only

---

## Walidacja koncowa

- [ ] `PAIR_REQ -> PAIR_RESP -> DATA -> ACK`
- [ ] `gateway -> node` plaintext i ciphertext
- [ ] `node -> gateway` plaintext i ciphertext
- [ ] Broadcast RX z wymuszona odpowiedzia, odpowiedz unicast do nadawcy
- [ ] Zanik zasilania podczas zapisu storage
- [ ] Dluzsza praca: RPi gateway + 2 nody

---

## Zarchiwizowane / nieaktualne zalozenia

- mesh / relay / forwarding jako cel produktu
- TTL w ramce aplikacyjnej
- stare `BEKO_NET_V1`
- `AuthTag 4 B`
- `XTEA-CTR`
- stare TODO zakladajace wiele relay-node'ow
