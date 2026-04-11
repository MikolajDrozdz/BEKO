# TODO – BEKO Pager Network (`pager-rtos`)

Ten plik zastępuje starsze TODO związane z topologią mesh i wcześniejszą wersją dokumentacji.
Nowy plan dotyczy aktualnej architektury: **1 Raspberry Pi Gateway + node’y STM32 w topologii gwiazdy**, ramki `BEKO_FRAME_V1`, obowiązkowego `ACK`, `AES-CTR`, `HMAC-SHA256`, `TPM-first` oraz maksymalnego `payload` 16 B.

---

## 1. Spójność dokumentacji i protokołu

- [ ] Poprawić dokumentację `README.md`, aby była całkowicie spójna z założeniami implementacyjnymi.
- [ ] Usunąć z dokumentacji pozostałości po topologii mesh, TTL, forwarding i RFM95W tam, gdzie projekt docelowo używa gwiazdy i SX1262.
- [ ] Ujednolicić nazwę ramki i protokołu: wszędzie stosować `BEKO_FRAME_V1`.
- [ ] Zweryfikować wszystkie rozmiary pól i końcową długość ramki.
- [ ] Dodać jeden tabelaryczny opis ramki używany jako źródło prawdy dla kodu i dokumentacji.

### Rekomendacje
- Pole `ver_type` ma 1 bajt, więc przy podziale 4 bity + 4 bity typ wiadomości może mieć tylko wartości `0x0..0xF`. W dokumentacji trzeba usunąć lub przeprojektować wpisy `0x10` i `0x11`.
- Pole `flags` ma 1 bajt, więc dopuszczalne są tylko bity `0..7`. Wpis `bit 8 – REMOVE` jest błędny i trzeba go usunąć albo przenieść do innego pola.
- Jeśli ma istnieć dodatkowa funkcja typu `REMOVE`, najlepiej przypisać ją do wolnego typu wiadomości zamiast do nieistniejącego bitu 8.

---

## 2. Finalizacja formatu ramki `BEKO_FRAME_V1`

- [ ] Zaimplementować finalny parser i serializer ramki zgodny z dokumentacją.
- [ ] Rozdzielić logikę dla ramek `DATA`, `ACK`, `RESP`, `PAIR_REQ`, `PAIR_RESP`, `CFG`, `COUNTER_SYNC`, `KEY_ROTATE`, `ERROR`.
- [ ] Zaimplementować walidację `payload_len` względem typu wiadomości.
- [ ] Dodać sprawdzenie zgodności `flags` z typem wiadomości.
- [ ] Dodać sprawdzanie dopuszczalnego `src_id`, `dst_id` i warunków broadcast.

### Rekomendacje
- Dla `ACK` i prostych odpowiedzi warto przyjąć krótsze, jawnie opisane payloady, np. 0 B lub 1–2 B, zamiast traktować wszystkie typy identycznie.
- Dla `CFG`, `COUNTER_SYNC` i `KEY_ROTATE` warto zdefiniować osobne mini-formaty payloadu, żeby uniknąć niejednoznacznej interpretacji danych.
- Warto dodać pole lub stałą domenową do budowy nonce AES-CTR, aby nie mieszać przestrzeni wiadomości pomiędzy różnymi typami ramek.

---

## 3. ACK i niezawodność dostarczenia

- [ ] Wymusić `ACK` dla każdej poprawnie odebranej wiadomości z gatewaya.
- [ ] Dodać timeout oczekiwania na `ACK` po stronie gatewaya.
- [ ] Dodać retransmisję z limitem prób.
- [ ] Dodać rozróżnienie: brak `ACK`, błędny `ACK`, spóźniony `ACK`, zduplikowany `ACK`.
- [ ] Dodać logikę mapowania `ACK` do `msg_id` i `counter`.

### Rekomendacje
- `ACK` powinien być uwierzytelniany HMAC-em tak samo jak zwykła wiadomość.
- Gateway powinien raportować operatorowi wynik: `dostarczono`, `brak ACK`, `błąd integralności`, `timeout`, `powtórzona ramka`.
- Dobrze dodać licznik retransmisji w panelu i logach.

---

## 4. Szyfrowanie wiadomości – AES-CTR sprzętowo

- [ ] Zastąpić wcześniejsze szyfrowanie rozwiązaniem opartym o sprzętowy blok AES w STM32U545.
- [ ] Zaimplementować `AES-CTR` dla pola `payload`.
- [ ] Zdefiniować jednoznaczny format nonce / counter block dla AES-CTR.
- [ ] Zapewnić niepowtarzalność pary: klucz + nonce.
- [ ] Dodać zerowanie buforów z plaintextem i kluczami po użyciu.

### Rekomendacje
- Najlepiej zbudować blok startowy AES-CTR z elementów takich jak `src_id`, `dst_id`, `msg_id`, `counter` oraz stała domenowa protokołu.
- Nie używać CBC dla krótkich wiadomości – CTR jest lepszy, bo nie wymaga paddingu i nie zwiększa długości payloadu.
- Najpierw należy zweryfikować HMAC, a dopiero potem odszyfrowywać payload.

---

## 5. HMAC-SHA256 – blok HASH sprzętowo

- [ ] Zaimplementować HMAC-SHA256 z użyciem sprzętowego bloku HASH w STM32U545.
- [ ] Ujednolicić listę pól wchodzących do HMAC.
- [ ] Dodać bezpieczne porównanie `mac_tag` po stronie odbiornika.
- [ ] Dodać testy zgodności HMAC z wersją referencyjną programową.

### Rekomendacje
- Źródłem prawdy dla HMAC powinno być: `ver_type || flags || src_id || dst_id || msg_id || counter || payload_len || payload`.
- Jeżeli TPM może wspierać ochronę klucza HMAC, to klucz nie powinien być ładowany do firmware w postaci jawnej.
- Dobrze utrzymywać osobne klucze dla szyfrowania i HMAC.

---

## 6. TPM-first i zarządzanie kluczami

- [ ] Zdefiniować finalny model przechowywania sekretów w TPM.
- [ ] Zaimplementować inicjalizację TPM przy starcie.
- [ ] Ustalić, które operacje są wykonywane bezpośrednio w TPM, a które tylko z jego wsparciem.
- [ ] Zaimplementować bezpieczne ładowanie / wyprowadzanie kluczy roboczych przy starcie.
- [ ] Wyczyścić z repo i kodu wszelkie jawne, stałe klucze testowe.

### Rekomendacje
- TPM powinien chronić sekret główny urządzenia i materiał do wyprowadzania kluczy sesyjnych.
- W RAM powinny przebywać tylko tymczasowe klucze robocze, i to możliwie krótko.
- Warto rozdzielić: klucz HMAC, klucz szyfrowania, materiał do pairingu i materiał do rotacji kluczy.

---

## 7. Anti-replay i licznik bezpieczeństwa

- [ ] Zaimplementować monotoniczny `counter` per relacja komunikacyjna.
- [ ] Zapisywać stan licznika w pamięci nieulotnej.
- [ ] Zaimplementować bezpieczny mechanizm `COUNTER_SYNC` tylko dla gatewaya.
- [ ] Dodać ochronę przed rollbackiem licznika po restarcie i zaniku zasilania.
- [ ] Dodać testy replay attack.

### Rekomendacje
- `COUNTER_SYNC` powinien być traktowany jako operacja administracyjna, logowana i ograniczona do administratora.
- Aktualizacja licznika w NVM powinna być odporna na zanik zasilania, np. przez podwójny rekord lub wersjonowanie.
- Przy odbiorze należy jasno rozróżnić: stary licznik, powtórzona ramka, przeskok licznika, ręczna synchronizacja.

---

## 8. Parowanie i relacja zaufania

- [ ] Zaimplementować finalny przebieg `PAIR_REQ` / `PAIR_RESP`.
- [ ] Usunąć pozostałości po starym modelu mesh / peer-to-peer, jeśli nie są już potrzebne.
- [ ] Zdecydować, czy warunek RSSI dla parowania rzeczywiście ma być częścią polityki bezpieczeństwa.
- [ ] Zaimplementować zapis relacji trusted w pamięci nieulotnej.
- [ ] Dodać procedurę usuwania zaufania i unieważnienia kluczy.

### Rekomendacje
- Wymóg `-20 dBm` dla parowania wygląda bardzo restrykcyjnie i może być trudny do spełnienia w praktyce; warto go zweryfikować eksperymentalnie albo zastąpić bardziej realistycznym warunkiem bliskości fizycznej lub trybem serwisowym.
- Parowanie powinno wymagać lokalnego potwierdzenia użytkownika na nodzie.
- Warto dodać timeout okna parowania i logowanie każdej próby parowania.

---

## 9. Rotacja kluczy – Diffie–Hellman

- [ ] Zdecydować o finalnym wariancie wymiany kluczy (klasyczny DH, ECDH, wsparcie TPM/PKA).
- [ ] Zaimplementować komunikaty `KEY_ROTATE`.
- [ ] Zaimplementować wyprowadzenie nowych kluczy po wymianie sekretu.
- [ ] Dodać mechanizm przełączenia ze starych kluczy na nowe.
- [ ] Dodać logowanie operacji rotacji kluczy.

### Rekomendacje
- Jeśli mikrokontroler lub TPM oferuje wygodniejsze wsparcie dla ECC, ECDH może być praktyczniejsze niż klasyczny DH.
- Należy jasno określić, czy rotacja dotyczy wszystkich node’ów, pojedynczego node’a czy tylko aktywnej sesji.
- Po rotacji stare klucze powinny być jawnie unieważnione i usunięte z RAM.

---

## 10. Warstwa radiowa SX1262 i niezawodność transmisji

- [ ] Uporządkować dokumentację i kod konfiguracji SX1262.
- [ ] Zweryfikować, czy włączone jest CRC warstwy radiowej.
- [ ] Opisać i udokumentować parametry modulacji: SF, BW, CR, preambuła, sync word, moc nadawania, timeout RX/TX.
- [ ] Dodać testy w warunkach zakłóceń i słabego sygnału.
- [ ] Dodać logi jakości sygnału: RSSI, SNR, błędy CRC, timeouty.

### Rekomendacje
- Warstwa radiowa powinna zapewniać wykrywanie błędów transmisji, a warstwa aplikacyjna – integralność kryptograficzną i uwierzytelnienie.
- Dobrze udokumentować, czy system używa LoRa, FSK, czy obu trybów w różnych scenariuszach.
- Warto oddzielić w logach: błąd radiowy, błąd HMAC, replay, brak ACK.

---

## 11. Role i kontrola dostępu

- [ ] Zaimplementować rozdzielenie uprawnień: administrator, operator, użytkownik, serwisant.
- [ ] Określić, które operacje są dozwolone dla każdej roli.
- [ ] Dodać osobne PIN-y lub osobne polityki dostępu dla ról administracyjnych i serwisowych.
- [ ] Logować działania uprzywilejowane.

### Rekomendacje
- Administrator powinien mieć dostęp do parowania, synchronizacji liczników, rotacji kluczy i ustawień krytycznych.
- Operator powinien mieć dostęp tylko do funkcji operacyjnych, które nie naruszają integralności systemu.
- Serwisant powinien działać w kontrolowanym trybie serwisowym z logowaniem dostępu fizycznego.

---

## 12. UART, diagnostyka i integralność startu

- [ ] Zaimplementować pełny log inicjalizacji po UART.
- [ ] Dodać pomiar czasu inicjalizacji krytycznych modułów.
- [ ] Dodać wykrywanie anomalii czasowych.
- [ ] Dodać kontrolę zgodności firmware i konfiguracji bezpieczeństwa przy starcie.
- [ ] Rozdzielić logi debug od logów produkcyjnych.

### Rekomendacje
- Nie logować kluczy ani pełnych danych wrażliwych.
- Dobrze dodać kody błędów lub krótkie stany diagnostyczne do łatwego filtrowania po UART.
- Warto wyraźnie oznaczać moduł, którego dotyczy błąd startu.

---

## 13. Panel webowy i warstwa gatewaya

- [ ] Zaimplementować prosty panel webowy na Raspberry Pi.
- [ ] Dodać listę node’ów, ich statusów i adresów.
- [ ] Dodać wysyłanie wiadomości do pojedynczego node’a i broadcast.
- [ ] Dodać prezentację wyniku operacji: `ACK`, timeout, błąd, brak odpowiedzi.
- [ ] Dodać historię wiadomości i odpowiedzi.

### Rekomendacje
- Panel powinien rozróżniać role administratora i operatora.
- Dobrze dodać czytelną prezentację ostatniego RSSI, czasu ostatniego ACK i stanu sparowania node’a.
- Operacje krytyczne, takie jak `PAIR_REQ`, `COUNTER_SYNC`, `KEY_ROTATE`, powinny być oddzielone od zwykłego wysyłania wiadomości.

---

## 14. Testy i walidacja

- [ ] Przygotować testy poprawnego doręczenia wiadomości.
- [ ] Przygotować testy ACK i retransmisji.
- [ ] Przygotować testy HMAC: poprawny, błędny, uszkodzony payload.
- [ ] Przygotować testy replay attack.
- [ ] Przygotować testy parowania i usuwania trusted.
- [ ] Przygotować testy restartu urządzenia i odtwarzania stanu z EEPROM / TPM.
- [ ] Przygotować testy utraty zasilania podczas zapisu.
- [ ] Przygotować testy radiowe z różnymi poziomami sygnału i zakłóceń.

### Rekomendacje
- Warto utrzymywać tabelę testów: scenariusz, warunki, wynik oczekiwany, wynik uzyskany.
- Testy bezpieczeństwa powinny być rozdzielone od testów funkcjonalnych.
- Dobrze dodać krótkie testy regresyjne dla parsera ramki i HMAC.

---

## 15. Najważniejsze korekty do wdrożenia w pierwszej kolejności

1. Uporządkować dokumentację: usunąć niespójności w `ver_type` i `flags`.
2. Zafinalizować format `BEKO_FRAME_V1` i parser ramki.
3. Dokończyć `ACK` + timeout + retransmisję po stronie gatewaya.
4. Wdrożyć sprzętowy `AES-CTR` i sprzętowy `HMAC-SHA256`.
5. Dopięć model `TPM-first` i bezpieczne ładowanie kluczy.
6. Wdrożyć trwały `counter` anti-replay i `COUNTER_SYNC`.
7. Dopięć pairing tylko z gatewayem.
8. Udokumentować i zweryfikować konfigurację SX1262.
9. Dodać testy bezpieczeństwa i niezawodności.

---

## 16. Elementy, które należy usunąć lub porzucić

- [ ] Stare założenia mesh / multi-hop / TTL.
- [ ] Dokumentację opartą o `BEKO_NET_V1`, `AuthTag 4B`, `XTEA-CTR` i routing wieloskokowy.
- [ ] Stare TODO związane z forwardingiem i flood relay.
- [ ] Niespójne typy wiadomości i flagi wykraczające poza rozmiar pól.
