# TODO - BEKO Pager Network (`pager-rtos`)

Ten plik zastępuje starsze TODO związane z topologią mesh i wcześniejszą wersją dokumentacji.
Aktualny stan repo dotyczy architektury: `1 Raspberry Pi Gateway + node'y STM32 w topologii gwiazdy`,
ramek `LAVIET_FRAME_V1`, `AES-CTR`, `HMAC-SHA256`, obowiązkowego `ACK` dla unicastu oraz modelu `TPM-first`.

Data aktualizacji: `2026-04-18`

---

## 1. Stan wdrożony w repo

- [x] Finalny format `LAVIET_FRAME_V1` i walidacja ramek.
- [x] Obsługa `DATA`, `ACK`, `RESP`, `PAIR_REQ`, `PAIR_RESP`, `CFG`, `COUNTER_SYNC`, `KEY_ROTATE`, `ERROR`.
- [x] Ograniczenie `payload` do `0..16 B`.
- [x] `AES-CTR` dla `payload`.
- [x] `HMAC-SHA256` dla całej części chronionej ramki.
- [x] `ACK` dla poprawnych ramek unicast oraz mapowanie `ACK -> msg_id + counter`.
- [x] Timeout oczekiwania na `ACK` w firmware node'a.
- [x] Retransmisja z limitem prób w firmware node'a.
- [x] Rozróżnienie `ACK`: poprawny / spóźniony / zduplikowany / nieoczekiwany / timeout.
- [x] Pairing sieciowy w modelu pasywnym po stronie node'a: node nasłuchuje, gateway inicjuje.
- [x] PIN użytkownika i oddzielny PIN administratora.
- [x] Dokumentacja kontraktu gatewaya w `gateway_docs/`.

---

## 2. Finalizacja formatu ramki `LAVIET_FRAME_V1`

- [x] Zaimplementować finalny parser i serializer ramki zgodny z dokumentacją.
- [x] Rozdzielić logikę dla ramek `DATA`, `ACK`, `RESP`, `PAIR_REQ`, `PAIR_RESP`, `CFG`, `COUNTER_SYNC`, `KEY_ROTATE`, `ERROR`.
- [x] Zaimplementować walidację `payload_len` względem typu wiadomości.
- [x] Dodać sprawdzenie zgodności `flags` z typem wiadomości.
- [x] Dodać sprawdzanie dopuszczalnego `src_id`, `dst_id` i warunków broadcast.

### Uwagi
- Broadcast został usunięty z normalnej ścieżki UI dla wysyłania wiadomości użytkownika.
- Pairing sieciowy nie jest już inicjowany przez node; `PAIR_REQ` dla sieci pochodzi od gatewaya.

---

## 3. ACK i niezawodność dostarczenia

- [x] Wymusić `ACK` dla każdej poprawnie odebranej wiadomości unicast z `ACK_REQUIRED`.
- [x] Dodać timeout oczekiwania na `ACK` w firmware.
- [x] Dodać retransmisję z limitem prób w firmware.
- [x] Dodać rozróżnienie: brak `ACK`, błędny / nieoczekiwany `ACK`, spóźniony `ACK`, zduplikowany `ACK`.
- [x] Dodać logikę mapowania `ACK` do `msg_id` i `counter`.
- [ ] Dodać scheduler retry / kolejkę retransmisji po stronie właściwego gatewaya Raspberry Pi.
- [ ] Dodać raportowanie stanu dostarczenia w panelu operatorskim gatewaya.

### Uwagi
- W tym repo działa logika niezawodności po stronie firmware STM32.
- Właściwy kod gatewaya Raspberry Pi nadal nie jest częścią tego repo; tutaj istnieje tylko kontrakt i dokumentacja.

---

## 4. Szyfrowanie wiadomości - AES-CTR sprzętowo

- [x] Zastąpić wcześniejsze szyfrowanie rozwiązaniem opartym o sprzętowy blok AES w STM32U545.
- [x] Zaimplementować `AES-CTR` dla pola `payload`.
- [x] Zdefiniować jednoznaczny format nonce / counter block dla AES-CTR.
- [x] Zapewnić niepowtarzalność pary: klucz + nonce.
- [x] Dodać zerowanie buforów z plaintextem i kluczami po użyciu.
- [x] Dodać runtime self-test sprzętowego AES względem referencji programowej.

### Otwarte
- [ ] Dodać osobny zestaw testów regresyjnych AES poza self-testem runtime.

---

## 5. HMAC-SHA256 - blok HASH sprzętowo

- [x] Zaimplementować HMAC-SHA256 z użyciem sprzętowego bloku HASH w STM32U545.
- [x] Ujednolicić listę pól wchodzących do HMAC.
- [x] Dodać bezpieczne porównanie `mac_tag` po stronie odbiornika.
- [x] Dodać runtime self-test zgodności HMAC ze ścieżką referencyjną programową.

### Otwarte
- [ ] Dodać zewnętrzne testy regresyjne / wektory HMAC poza firmware.

---

## 6. TPM-first i zarządzanie kluczami

- [ ] Zdefiniować finalny model przechowywania sekretów w TPM.
- [x] Zaimplementować inicjalizację TPM przy starcie.
- [ ] Ustalić, które operacje są wykonywane bezpośrednio w TPM, a które tylko z jego wsparciem.
- [x] Zaimplementować bezpieczne ładowanie / wyprowadzanie kluczy roboczych przy starcie.
- [x] Wyczyścić z repo i kodu jawne, stałe klucze testowe.
- [x] Przełączyć derivation kluczy na HMAC-based KDF.
- [x] Dodać bootstrap `TPM-first`, z fallbackiem do entropii z TPM RNG / HAL RNG.
- [ ] Dokończyć trwałe przechowywanie root seeda w TPM NV.

### Uwagi
- Aktualna logika jest już `TPM-first`, ale backend NV w lokalnym driverze TPM nadal ma status scaffold / `ENOTSUP`.
- To oznacza, że pełny model trwałej persystencji root seeda w TPM nie jest jeszcze domknięty.

---

## 7. Anti-replay i licznik bezpieczeństwa

- [x] Zaimplementować monotoniczny `counter` per relacja komunikacyjna.
- [x] Zapisywać stan licznika w pamięci nieulotnej.
- [x] Zaimplementować bezpieczny mechanizm `COUNTER_SYNC` tylko dla gatewaya.
- [ ] Dodać ochronę przed rollbackiem licznika po restarcie i zaniku zasilania.
- [ ] Dodać testy replay attack.

---

## 8. Parowanie i relacja zaufania

- [x] Zaimplementować finalny przebieg `PAIR_REQ` / `PAIR_RESP`.
- [x] Uporządkować model bez starego mesh / relay / TTL.
- [x] Zostawić lokalną zgodę użytkownika na nodzie.
- [x] Zaimplementować zapis relacji trusted w pamięci nieulotnej.
- [x] Dodać procedurę usuwania zaufania i unieważnienia kluczy.
- [x] Zmienić network pairing tak, aby node tylko nasłuchiwał, a nie nadawał własnego sygnału parowania.

### Otwarte
- [ ] Przetestować praktycznie politykę RSSI i zdecydować, czy ma mieć znaczenie blokujące czy tylko diagnostyczne.

---

## 9. Rotacja kluczy

- [ ] Zdecydować o finalnym wariancie wymiany kluczy.
- [ ] Zaimplementować komunikaty `KEY_ROTATE`.
- [ ] Zaimplementować wyprowadzenie nowych kluczy po wymianie sekretu.
- [ ] Dodać mechanizm przełączenia ze starych kluczy na nowe.
- [ ] Dodać logowanie operacji rotacji kluczy.

---

## 10. Warstwa radiowa SX1276/RFM95 i niezawodność transmisji

- [x] Uporządkować dokumentację i kod konfiguracji SX1276/RFM95.
- [ ] Zweryfikować, czy włączone jest CRC warstwy radiowej dla wszystkich używanych trybów.
- [ ] Opisać i udokumentować parametry modulacji: `SF`, `BW`, `CR`, preambuła, `sync word`, moc nadawania, timeout `RX/TX`.
- [ ] Dodać testy w warunkach zakłóceń i słabego sygnału.
- [ ] Dodać logi jakości sygnału: `RSSI`, `SNR`, błędy CRC, timeouty.

---

## 11. Role i kontrola dostępu

- [ ] Zaimplementować pełny rozdział uprawnień: administrator, operator, użytkownik, serwisant.
- [ ] Określić, które operacje są dozwolone dla każdej roli.
- [x] Dodać osobne PIN-y dla wejścia użytkownika i administratora.
- [ ] Logować działania uprzywilejowane.

### Uwagi
- Obecny stan to rozdzielenie `USER PIN` i `ADMIN PIN`.
- To nie jest jeszcze pełny model ról i audytu.

---

## 12. UART, diagnostyka i integralność startu

- [ ] Zaimplementować pełny log inicjalizacji po UART.
- [ ] Dodać pomiar czasu inicjalizacji krytycznych modułów.
- [ ] Dodać wykrywanie anomalii czasowych.
- [ ] Dodać kontrolę zgodności firmware i konfiguracji bezpieczeństwa przy starcie.
- [ ] Rozdzielić logi debug od logów produkcyjnych.

---

## 13. Gateway Raspberry Pi

- [x] Opisać kontrakt komunikacyjny i rekomendowaną architekturę w `gateway_docs/`.
- [ ] Zaimplementować właściwy proces gatewaya na Raspberry Pi.
- [ ] Dodać obsługę retry / ACK status / timeout po stronie gatewaya.
- [ ] Dodać panel operatorski lub interfejs serwisowy.
- [ ] Zintegrować gateway z tym samym modułem radiowym `SX1276/RFM95`.

---

## 14. Testy i walidacja

- [ ] Przygotować testy poprawnego doręczenia wiadomości.
- [ ] Przygotować testy ACK i retransmisji end-to-end.
- [ ] Przygotować testy HMAC: poprawny, błędny, uszkodzony payload.
- [ ] Przygotować testy replay attack.
- [ ] Przygotować testy parowania i usuwania trusted.
- [ ] Przygotować testy restartu urządzenia i odtwarzania stanu z EEPROM / TPM.
- [ ] Przygotować testy utraty zasilania podczas zapisu.
- [ ] Przygotować testy radiowe z różnymi poziomami sygnału i zakłóceń.

### Uwagi
- W firmware istnieją już self-testy crypto przy starcie.
- Nadal brakuje pełnego zestawu testów regresyjnych i scenariuszy end-to-end.

---

## 15. Priorytety na kolejne wdrożenia

1. Dokończyć trwałe przechowywanie root seeda w TPM NV.
2. Dodać ochronę countera przed rollbackiem po zaniku zasilania.
3. Zaimplementować właściwy gateway Raspberry Pi z retry / timeout / statusem ACK.
4. Zaimplementować `KEY_ROTATE`.
5. Zrobić pełne testy end-to-end i walidację radiową.

---

## 16. Elementy zarchiwizowane

- [x] Stare założenia mesh / multi-hop / TTL.
- [x] Dokumentacja oparta o `BEKO_NET_V1`, `AuthTag 4B`, `XTEA-CTR` i routing wieloskokowy.
- [x] Stare TODO związane z forwardingiem i flood relay.
- [x] Niespójne typy wiadomości i flagi wykraczające poza rozmiar pól.
