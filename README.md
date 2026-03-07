# BEKO podstawowy program z inicjalizacjami peryferii

## Struktura projektu

Pliki są podzielone 2 części, te do pisania aplikacji i te automatycznie tworzone przez IDE.

Aplikacja:
- /Core/App: **Tutaj pisze się program w stylu Arduino** (tj. inicjalizacja ~ app_init i pętra główna ~ app_main)

Automatycznie stworzone przez IDE:
- /Core/Src: defaultowa lokalizacja plików .c do projektu razem z *main.c*
- /Core/Inc: defaultowa lokalizacja plików .h do projektu razem z *main.h*

| Peryferium      | Status    | Uwagi |
|:----------|:---------:| ------:|
| LCD   | OK ✅ | |
| BMP280 | OK ✅ | Nie jest dostępne na wszystkich płytkach, potrzebna naprawa |
| TOF | OK ✅ | Uwaga! funckja blokująca na ok. 242 ms, może być zaimplenetowany w inny sposób. INT nie jest aktywny, jest zostawiony dla radia. |
| RADIO | OK ✅ | |
| MIC | NO ❌ | |
| Speaker | In progess ⚒️ | Trzeba dodać głośnik |
| ACC | NO ❌ | Trzeba podłączyć |
| TMP | OK ✅ | |
| BUTTON | OK ✅ | |
| I2C MEM |  OK ✅ | Służy do zapamiętywania urządzeń |
| HALL SENSOR | NO ❌ | |
| LIGHT SENSOR| NO ❌ | |
| OLED DISP | NO ❌ | |
| SERVO | NO ❌ | |
| LED DISP | NO ❌ | |
| LED ARRAY | OK ✅  | |

## Bezpieczeństwo komunikacji (BEKO_NET_V1)

### Co jest chronione

- poufność wiadomości `USER` pomiędzy sparowanymi (zaufanymi) urządzeniami,
- podstawowa kontrola integralności/uwierzytelnienia klucza przed odszyfrowaniem treści,
- trwałość listy zaufanych urządzeń po restarcie (EEPROM),
- rozróżnienie ruchu systemowego i użytkownika.

### Przebieg transmisji `USER` (secure mode)

1. Nadajnik buduje ramkę `BEKO_NET_V1` (`src_id`, `dst_id`, `msg_id`, `ttl`, `payload`).
2. Dla `USER` pobiera klucz per-peer (z `security_main`, wyprowadzany z kodu parowania i ID węzłów).
3. Treść wiadomości jest szyfrowana (`XTEA-CTR`).
4. Po preambule/ramce radiowej pierwsze bajty danych aplikacyjnych to `AuthTag` (4B) wyliczony z:
   - klucza per-peer,
   - pól ramki (`type`, `src_id`, `dst_id`, `msg_id`),
   - zaszyfrowanego payloadu.
5. Dopiero po `AuthTag` idzie zaszyfrowana wiadomość.
6. Odbiornik najpierw weryfikuje `AuthTag`; jeśli poprawny, odszyfrowuje payload i przekazuje dalej.
7. Gdy tag nie pasuje, ramka jest odrzucana (bez deszyfrowania i bez wyświetlenia treści).

### Zaufane urządzenia i parowanie

- Parowanie używa kodu jednorazowego (JOIN_REQ/JOIN_ACCEPT).
- Po akceptacji urządzenie trafia do listy trusted i jest zapisywane do EEPROM (`i2c_mem_store`).
- Po usunięciu trusted lokalnie wysyłana jest do drugiej strony ramka `TRUST_REMOVED`.
- Kodowanie (`coding`) jest domyślnie aktywne po starcie, więc ruch `USER` bez poprawnego klucza jest ignorowany.

### TPM i klucze

- `security_main` inicjalizuje ST33KTPM2X i próbuje pobrać losowy seed z TPM.
- Seed oraz runtime config są trzymane w EEPROM i odtwarzane po restarcie.
- Z seeda wyprowadzany jest klucz sieciowy (dla ramek systemowych).
- Dla ruchu `USER` używany jest klucz per-peer wyprowadzany z danych parowania.

## Przykładowy scenariusz komunikacji

1. Urządzenie A uruchamia tryb parowania i wysyła `JOIN_REQ` z kodem jednorazowym.
2. Urządzenie B odbiera `JOIN_REQ`, użytkownik akceptuje parowanie, B zapisuje A jako trusted i odsyła `JOIN_ACCEPT`.
3. A odbiera `JOIN_ACCEPT`, weryfikuje kod, zapisuje B jako trusted (EEPROM).
4. A wysyła wiadomość `USER` do B:
   - wyznacza klucz per-peer,
   - szyfruje payload (`XTEA-CTR`),
   - oblicza `AuthTag` (4B) i dokleja go przed ciphertextem.
5. B odbiera ramkę i wykonuje:
   - sprawdzenie, czy nadawca jest trusted,
   - weryfikację `AuthTag`,
   - dopiero po poprawnej weryfikacji odszyfrowanie payloadu i wyświetlenie treści.
6. Jeśli `AuthTag` się nie zgadza, B odrzuca ramkę (brak wyświetlenia i brak dalszego przetwarzania).
7. Jeśli A usuwa B z trusted, A wysyła `TRUST_REMOVED`, a B usuwa A ze swojej listy po odebraniu tej ramki.

## Słabe punkty i możliwe ataki

### Ograniczenia obecnej implementacji

- `AuthTag` ma 32 bity i jest lekki obliczeniowo (nie jest pełnym MAC typu HMAC-SHA256).
- Kod parowania ma małą entropię (krótki kod cyfr), więc przy przechwyceniu procesu parowania rośnie ryzyko ataku offline.
- Brak pełnej ochrony przed aktywnym jammerem (zakłócanie pasma).
- Brak forward secrecy: kompromitacja danych pairing/trusted może odsłonić historyczne wiadomości zapisane z eteru.
- Anti-replay opiera się głównie o `msg_id` + dedup cache (okno czasowe), nie o globalny licznik kryptograficzny.

### Przykładowe scenariusze ataku

- `DoS/Jamming`: napastnik zagłusza kanał; brak skutecznego odbioru mimo poprawnej kryptografii.
- `Replay w oknie`: ponowne nadanie tej samej ramki zanim dedup ją odfiltruje lub po wygaśnięciu okna.
- `Podsłuch + analiza`: metadane (`src/dst/msg_id/typ`) są jawne, nawet gdy payload jest szyfrowany.
- `Przejęcie parowania`: jeśli ktoś zna/zgadnie kod parowania w trakcie JOIN, może dodać nieautoryzowane urządzenie.

### Rekomendacje hardeningu (kolejne kroki)

1. Zastąpić obecny `AuthTag` silnym MAC (np. HMAC-SHA256, min. 64 bity tagu).
2. Wydłużyć i losowość kodu parowania (np. 128-bit challenge zamiast krótkiego kodu cyfr).
3. Dodać licznik anty-replay per-peer (monotoniczny, zapisywany w NVM).
4. Ograniczyć metadane jawne lub dodać rotację identyfikatorów.
5. Dodać politykę re-key (rotacja kluczy per-peer po czasie/liczbie ramek).
