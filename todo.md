# Dokumentacja projektu – bezpieczna sieć pagerowa oparta o STM32, RFM95W i Raspberry Pi Zero

## 1. Cel projektu

Celem projektu jest implementacja lekkiej, bezprzewodowej sieci pagerowej umożliwiającej przesyłanie krótkich komunikatów tekstowych do urządzeń klienckich oraz odbieranie prostych odpowiedzi zwrotnych z poziomu fizycznych przycisków.

Projekt ma odpowiadać na potrzeby dyskretnej i niezawodnej komunikacji w środowisku pracy, np. pomiędzy kuchnią i kelnerem w restauracji albo pomiędzy pracownikami hali produkcyjnej. Wiadomości są wysyłane z poziomu panelu webowego uruchomionego na Raspberry Pi Zero, a następnie rozsyłane drogą radiową do węzłów końcowych opartych o mikrokontrolery STM32 i moduły RFM95W-862S2.

Najważniejszym założeniem projektu jest bezpieczeństwo transmisji. System ma zapewniać poufność wiadomości, ochronę przed modyfikacją ramek, podstawowe zabezpieczenie przed powtórzeniem starej transmisji oraz kontrolę listy zaufanych urządzeń. Dodatkowo każdy węzeł jest wyposażony w moduł ST33KTPM2X32DKG9, który ma wspierać generowanie i ochronę materiału kluczowego.

---

## 2. Scenariusz użycia systemu

W przykładowym scenariuszu Raspberry Pi Zero pełni rolę centralnego punktu zarządzania systemem. Na Raspberry Pi działa prosty panel webowy dostępny przez Wi-Fi, z którego operator może:

- wybrać adresata wiadomości,
- wpisać krótki komunikat tekstowy,
- wysłać wiadomość do wybranego węzła,
- śledzić status dostarczenia,
- odebrać odpowiedź od użytkownika urządzenia końcowego.

Urządzenie końcowe STM32 po odebraniu wiadomości:

- sprawdza, czy wiadomość jest skierowana do niego,
- weryfikuje jej autentyczność i integralność,
- odszyfrowuje treść,
- wyświetla komunikat na ekranie,
- umożliwia odpowiedź jednym z trzech przycisków, np.:
  - `TAK`,
  - `NIE`,
  - `OK` / `PRZYJĄŁEM`.

Jeżeli wiadomość nie jest przeznaczona dla danego węzła, a jej licznik `TTL` jest większy od zera, węzeł może przekazać ją dalej. Dzięki temu system może działać w trybie prostego, wieloskokowego routingu typu WSN / mesh relay.

---

## 3. Infrastruktura systemu

## 3.1. Elementy sprzętowe

System składa się z następujących elementów:

### Węzeł główny
- **Raspberry Pi Zero**
- moduł radiowy zgodny z rodziną **RFM95W-862S2**
- interfejs Wi-Fi do obsługi panelu webowego
- oprogramowanie zarządzające wysyłką i odbiorem komunikatów

### Węzły klienckie
- **STM32**
- moduł radiowy **RFM95W-862S2**
- wyświetlacz do prezentacji wiadomości
- 3 przyciski do odpowiedzi predefiniowanych
- pamięć EEPROM / NVM do przechowywania konfiguracji
- moduł bezpieczeństwa **ST33KTPM2X32DKG9**
- opcjonalnie buzzer / LED do sygnalizacji nowej wiadomości

## 3.2. Topologia logiczna

System ma charakter **hybrydowy**:

- logicznie posiada punkt centralny zarządzania na Raspberry Pi,
- radiowo działa jako **sieć wieloskokowa**, gdzie węzły mogą przekazywać dalej komunikaty.

W praktyce Raspberry Pi jest źródłem większości wiadomości użytkowych, natomiast węzły STM32 pełnią jednocześnie role:

- odbiorników końcowych,
- przekaźników ramek,
- nadajników odpowiedzi zwrotnych.

---

## 4. Założenia projektowe

W systemie przyjęto następujące założenia:

- bardzo krótka wiadomość użytkowa,
- maksymalna długość ramki aplikacyjnej: **64 bajty**,
- możliwość działania w paśmie radiowym z użyciem RFM95W,
- wsparcie dla transmisji:
  - **LoRa**,
  - **FSK**,
- obowiązkowe potwierdzenie odbioru wiadomości,
- podstawowy mechanizm multi-hop,
- wysoki priorytet bezpieczeństwa,
- trwałość informacji o zaufanych urządzeniach po restarcie,
- minimalna złożoność obsługi po stronie użytkownika końcowego.

---

## 5. Architektura systemu

System można podzielić na 4 warstwy funkcjonalne:

## 5.1. Warstwa webowa
Uruchomiona na Raspberry Pi Zero. Odpowiada za:

- logowanie użytkownika do panelu,
- tworzenie wiadomości,
- wybór adresata,
- podgląd statusów dostarczenia,
- prezentację odpowiedzi z węzłów,
- zarządzanie parowaniem urządzeń.

## 5.2. Warstwa aplikacyjna
Definiuje typy komunikatów, logikę routingu i zachowanie systemu. Przykładowe typy ramek:

- `USER_MSG` – wiadomość tekstowa do użytkownika,
- `ACK` – potwierdzenie odebrania,
- `RESP` – odpowiedź z przycisku,
- `JOIN_REQ` – żądanie parowania,
- `JOIN_ACCEPT` – akceptacja parowania,
- `TRUST_REMOVED` – usunięcie zaufania,
- `HELLO` / `BEACON` – diagnostyka lub wykrywanie obecności.

## 5.3. Warstwa bezpieczeństwa
Zapewnia:

- wyprowadzanie kluczy,
- szyfrowanie danych,
- generowanie i weryfikację MAC,
- anti-replay,
- przechowywanie zaufanych peerów,
- współpracę z TPM.

## 5.4. Warstwa radiowa
Odpowiada za transmisję przez RFM95W, w tym:

- konfigurację LoRa / FSK,
- nadawanie i odbiór ramek,
- retransmisję,
- zarządzanie kanałem radiowym,
- podstawowy mechanizm przekazywania dalej.

---

## 6. Działanie systemu

## 6.1. Przepływ wiadomości od panelu webowego do węzła

1. Operator otwiera panel webowy na Raspberry Pi.
2. Wybiera urządzenie docelowe lub grupę urządzeń.
3. Wpisuje krótki komunikat tekstowy.
4. Raspberry Pi buduje ramkę aplikacyjną.
5. Dla wiadomości typu `USER_MSG` dobierany jest klucz per-peer.
6. Treść wiadomości zostaje zaszyfrowana.
7. Do ramki dodawany jest MAC.
8. Ramka zostaje nadana przez moduł radiowy.
9. Węzeł pośredni:
   - odbiera ramkę,
   - sprawdza, czy już ją widział,
   - zmniejsza `TTL`,
   - przekazuje dalej, jeśli nie jest adresatem końcowym.
10. Węzeł docelowy:
   - weryfikuje autentyczność,
   - sprawdza anti-replay,
   - odszyfrowuje wiadomość,
   - wyświetla ją użytkownikowi,
   - odsyła `ACK`.
11. Użytkownik może wysłać odpowiedź przez przycisk.
12. Odpowiedź wraca do Raspberry Pi jako ramka `RESP`.

## 6.2. Zachowanie węzła końcowego

Po odebraniu poprawnej wiadomości węzeł:

- zapisuje `msg_id` / `counter` do mechanizmu deduplikacji,
- wyświetla treść,
- generuje lokalny sygnał (np. buzzer / LED),
- oczekuje na reakcję użytkownika,
- po naciśnięciu przycisku wysyła odpowiedź.

## 6.3. Forwarding wiadomości

Jeśli węzeł nie jest adresem docelowym:

- sprawdza, czy ramka nie została już przetworzona,
- sprawdza `TTL`,
- po krótkim losowym opóźnieniu retransmituje ramkę.

Takie podejście zmniejsza ryzyko lawinowego floodingu przy większej liczbie urządzeń.

---

## 7. Obecne mechanizmy zabezpieczające

W obecnym systemie chronione są następujące obszary:

- poufność wiadomości `USER`,
- podstawowa integralność i uwierzytelnienie,
- trwałość listy trusted po restarcie,
- logiczne rozróżnienie ramek systemowych i użytkowych.

## 7.1. Aktualny przebieg transmisji `USER`

1. Nadajnik buduje ramkę `BEKO_NET_V1` zawierającą:
   - `src_id`,
   - `dst_id`,
   - `msg_id`,
   - `ttl`,
   - `payload`.
2. Pobierany jest klucz per-peer.
3. Payload szyfrowany jest algorytmem `XTEA-CTR`.
4. Wyliczany jest `AuthTag` 4B na podstawie:
   - klucza per-peer,
   - pól nagłówka,
   - zaszyfrowanego payloadu.
5. Do transmisji wysyłany jest `AuthTag`, a następnie ciphertext.
6. Odbiornik najpierw weryfikuje `AuthTag`.
7. Dopiero po poprawnej weryfikacji odszyfrowuje treść.

---

## 8. Proponowane ulepszenia bezpieczeństwa

Ze względu na ograniczenia obecnej implementacji należy rozszerzyć system o kilka istotnych mechanizmów.

## 8.1. Silniejszy MAC

Obecny `AuthTag` ma 32 bity, co jest zbyt małą wartością dla systemu, który ma być uznany za bezpieczny.

### Propozycja
Zastąpić `AuthTag` mechanizmem:
- `HMAC-SHA256` z obcięciem do **8 bajtów**, albo
- `AES-CMAC` z obcięciem do **8 bajtów**, jeśli implementacja AES będzie wygodniejsza.

### Uzasadnienie
8-bajtowy tag daje znacznie lepszą odporność niż 4 bajty, a nadal pozwala zmieścić się w limicie 64 bajtów.

## 8.2. Silniejsze parowanie

Zamiast krótkiego kodu cyfr należy zastosować:
- losowy challenge 128-bit,
- opcjonalnie wyświetlenie skrótu lub krótkiego kodu porównawczego dla użytkownika,
- potwierdzenie parowania przez fizyczny przycisk.

Takie podejście znacząco utrudnia atak offline.

## 8.3. Anti-replay per-peer

Należy dodać:
- monotoniczny licznik nadawcy,
- okno akceptacji po stronie odbiorcy,
- zapis ostatniego zaakceptowanego licznika w NVM.

To pozwoli blokować powtórne odtworzenie starszych ramek.

## 8.4. Ograniczenie jawnych metadanych

W obecnej wersji część pól nagłówka jest jawna. To upraszcza routing, ale ułatwia analizę ruchu.

### Możliwe podejście
- pozostawić jawne tylko pola niezbędne do routingu,
- dodać pseudonimowe identyfikatory sesyjne,
- okresowo rotować identyfikatory logiczne.

## 8.5. Re-key

Należy wprowadzić politykę rotacji kluczy per-peer:
- po określonej liczbie ramek,
- po określonym czasie,
- po ponownym parowaniu,
- po wykryciu incydentu bezpieczeństwa.

---

## 9. Ograniczenie 64 bajtów i konsekwencje projektowe

Najważniejsze ograniczenie projektu to maksymalny rozmiar ramki aplikacyjnej wynoszący **64 bajty**. Oznacza to, że wszystkie pola nagłówka, bezpieczeństwa i danych użytkownika muszą zmieścić się w tym limicie.

W praktyce należy rozdzielić typy ramek na:

- **ramki użytkowe** – zoptymalizowane pod krótkie komunikaty,
- **ramki systemowe / parujące** – również mieszczące się w 64 bajtach, ale o mniejszym polu danych.

Nie ma potrzeby, aby każda ramka przenosiła 128-bit challenge. Taki challenge powinien być obecny tylko w ramkach parowania.

---

## 10. Proponowany format ramki

Poniżej przedstawiono rekomendowaną ramkę aplikacyjną dla wiadomości użytkowych.

## 10.1. Ramka `USER_MSG` / `ACK` / `RESP`

| Pole | Rozmiar | Opis |
|---|---:|---|
| `ver_type` | 1 B | wersja protokołu + typ wiadomości |
| `flags` | 1 B | bity sterujące: ACK required, forwarded, encrypted, response itp. |
| `src_id` | 2 B | identyfikator źródła |
| `dst_id` | 2 B | identyfikator celu |
| `msg_id` | 2 B | identyfikator wiadomości |
| `ttl` | 1 B | liczba pozostałych skoków |
| `counter` | 4 B | licznik anty-replay per-peer |
| `payload_len` | 1 B | długość payloadu |
| `payload` | 0–34 B | dane użytkownika / odpowiedź |
| `mac_tag` | 8 B | skrócony MAC, np. HMAC-SHA256-64 |
| `reserved` | dopełnienie | opcjonalne pole przyszłej rozbudowy |

### Suma przykładowa
Nagłówek stały bez payloadu i bez rezerwy:
- 1 + 1 + 2 + 2 + 2 + 1 + 4 + 1 + 8 = **22 bajty**

Daje to:
- **42 bajty** wolne w limicie 64 B,
- praktycznie bezpiecznie można przyjąć **payload do 32–34 bajtów**.

To jest rozsądna długość dla pagera tekstowego, np.:
- `STANOWISKO 4`,
- `PRZYJDZ TERAZ`,
- `ZAMOWIENIE GOTOWE`,
- `TAK`,
- `NIE`,
- `OK`.

## 10.2. Ramka `JOIN_REQ` / `JOIN_ACCEPT`

Dla ramek parowania można przyjąć osobny układ:

| Pole | Rozmiar | Opis |
|---|---:|---|
| `ver_type` | 1 B | wersja + typ `JOIN_*` |
| `flags` | 1 B | bity sterujące |
| `src_id` | 2 B | identyfikator źródła |
| `dst_id` | 2 B | identyfikator celu lub broadcast lokalny |
| `msg_id` | 2 B | identyfikator |
| `ttl` | 1 B | liczba skoków |
| `pair_nonce` | 16 B | challenge 128-bit |
| `pair_info` | 4–8 B | dane pomocnicze, np. capabilities |
| `mac_tag` | 8 B | MAC |
| `optional` | reszta | zależnie od etapu parowania |

Taki układ nadal mieści się w 64 bajtach.

---

## 11. Opis pól ramki

## 11.1. `ver_type`
Pole łączy wersję protokołu i typ ramki. Pozwala rozróżnić:
- `USER_MSG`,
- `ACK`,
- `RESP`,
- `JOIN_REQ`,
- `JOIN_ACCEPT`,
- `TRUST_REMOVED`.

## 11.2. `flags`
Służy do sygnalizacji zachowania ramki:
- czy wymaga potwierdzenia,
- czy jest zaszyfrowana,
- czy została forwardowana,
- czy zawiera odpowiedź przycisku.

## 11.3. `src_id` i `dst_id`
Identyfikatory urządzeń. W przyszłości mogą zostać zastąpione przez pseudonimy sesyjne.

## 11.4. `msg_id`
Identyfikator logiczny wiadomości, używany m.in. do:
- korelacji `ACK`,
- deduplikacji,
- śledzenia retransmisji.

## 11.5. `ttl`
Chroni sieć przed nieskończonym krążeniem ramek.

## 11.6. `counter`
Monotoniczny licznik bezpieczeństwa per-peer. Stanowi kluczowy element ochrony anti-replay.

## 11.7. `payload_len`
Umożliwia interpretację długości danych użytkowych.

## 11.8. `payload`
W przypadku `USER_MSG` zawiera wiadomość tekstową.
W przypadku `RESP` może zawierać:
- kod odpowiedzi,
- opcjonalny krótki komentarz,
- status.

## 11.9. `mac_tag`
Skrócony MAC zapewniający:
- integralność,
- uwierzytelnienie nadawcy,
- powiązanie danych z nagłówkiem i ciphertextem.

---

## 12. Proponowane szyfrowanie i uwierzytelnianie

## 12.1. Wariant minimalnej ingerencji
Jeśli chcesz zachować obecną architekturę:

- szyfrowanie: `XTEA-CTR`,
- uwierzytelnianie: `HMAC-SHA256` obcięty do 8 bajtów.

To podejście jest najłatwiejsze do wdrożenia jako ewolucja obecnego projektu.

## 12.2. Wariant bardziej docelowy
Jeżeli zasoby STM32 i złożoność implementacji na to pozwolą, lepiej rozważyć:
- `AES-CTR + CMAC`,
- albo nowoczesny AEAD, np. `Ascon-128a`, jeśli chcesz mieć szyfrowanie i integralność w jednym mechanizmie.

Dla projektu studenckiego i istniejącej bazy kodu sensowne jest jednak podejście ewolucyjne, czyli pozostanie przy aktualnym szyfrowaniu i wzmocnienie MAC.

---

## 13. Rola TPM ST33KTPM2X32DKG9

Moduł TPM może pełnić w systemie następujące role:

- źródło losowości do generowania seeda,
- źródło nonce do parowania,
- bezpieczne powiązanie urządzenia z materiałem kluczowym,
- potwierdzanie działań administracyjnych przez przycisk `TPM_PP`,
- wsparcie przy inicjalizacji zaufania po starcie.

### Zalecany model
TPM nie musi wykonywać całej kryptografii runtime dla każdej ramki. Wystarczy, że:
- generuje seed,
- uczestniczy w inicjalizacji kluczy,
- zabezpiecza operacje krytyczne,
- pomaga w budowaniu zaufania do urządzenia.

To jest realistyczne dla projektu o ograniczonych zasobach.

---

## 14. Parowanie urządzeń

## 14.1. Cel parowania
Parowanie służy do:
- ustanowienia relacji zaufania,
- uzgodnienia materiału wejściowego do klucza per-peer,
- zapisania partnera na liście trusted.

## 14.2. Proponowany przebieg parowania

1. Urządzenie A wchodzi w tryb parowania.
2. Generuje `pair_nonce_A` z użyciem TPM lub RNG.
3. Wysyła `JOIN_REQ`.
4. Urządzenie B odbiera `JOIN_REQ`.
5. Użytkownik B zatwierdza parowanie przyciskiem.
6. B generuje `pair_nonce_B`.
7. B wyprowadza wspólny materiał kluczowy z:
   - `pair_nonce_A`,
   - `pair_nonce_B`,
   - `src_id`,
   - `dst_id`,
   - lokalnego seeda.
8. B zapisuje A jako trusted.
9. B odsyła `JOIN_ACCEPT`.
10. A weryfikuje odpowiedź i zapisuje B jako trusted.
11. Obie strony odkładają dane do EEPROM / NVM.

## 14.3. Co zapisywać po parowaniu

Dla każdego peer-a warto przechowywać:

- `peer_id`,
- status trusted,
- materiał do wyprowadzenia klucza lub gotowy klucz per-peer,
- ostatni zaakceptowany `counter_rx`,
- ostatni użyty `counter_tx`,
- znacznik czasu / licznik rotacji klucza,
- flagi polityki bezpieczeństwa.

---

## 15. Usuwanie parowania

Usuwanie relacji trusted musi działać dwustronnie.

## 15.1. Proponowany scenariusz

1. Użytkownik na urządzeniu A usuwa B z listy trusted.
2. A lokalnie kasuje zaufanie i materiał kluczowy związany z B.
3. A wysyła do B ramkę `TRUST_REMOVED`.
4. Po odebraniu i zweryfikowaniu tej ramki B usuwa A ze swojej listy trusted.
5. Obie strony aktualizują EEPROM / NVM.

## 15.2. Uwagi praktyczne
Jeżeli `TRUST_REMOVED` nie zostanie dostarczone:
- A i tak uznaje B za niezaufane,
- B może nadal uważać A za trusted do czasu ręcznego usunięcia lub timeoutu polityki.

Dlatego warto przewidzieć:
- lokalne usuwanie natychmiastowe,
- synchronizację z drugą stroną jako mechanizm dodatkowy.

---

## 16. Przykładowy scenariusz komunikacji użytkowej

1. Raspberry Pi wysyła wiadomość do węzła `NODE_03`:
   - treść: `PRZYJDZ DO STREFY A`.
2. Budowana jest ramka `USER_MSG`.
3. Dobierany jest klucz per-peer dla `RPI -> NODE_03`.
4. Payload jest szyfrowany.
5. Obliczany jest `mac_tag`.
6. Ramka zostaje wysłana do sieci.
7. `NODE_01` odbiera ramkę:
   - widzi, że `dst_id != NODE_01`,
   - zmniejsza `ttl`,
   - przekazuje dalej.
8. `NODE_03` odbiera ramkę:
   - weryfikuje MAC,
   - sprawdza `counter`,
   - odszyfrowuje treść,
   - wyświetla wiadomość,
   - odsyła `ACK`.
9. Użytkownik naciska przycisk `TAK`.
10. `NODE_03` buduje ramkę `RESP`.
11. Odpowiedź wraca do Raspberry Pi.
12. Panel webowy pokazuje status:
   - dostarczono,
   - odpowiedź: `TAK`.

---

## 17. Słabe punkty obecnej implementacji

Aktualna wersja systemu ma następujące ograniczenia:

- `AuthTag` 32-bit jest zbyt krótki,
- kod parowania ma zbyt małą entropię,
- brak pełnego, trwałego anti-replay per-peer,
- brak forward secrecy,
- metadane w nagłówku są jawne,
- aktywny jammer nadal może zakłócić komunikację,
- forwarding może generować nadmiarowy ruch bez dodatkowych ograniczeń.

---

## 18. Rekomendacje implementacyjne

## 18.1. Co wdrożyć w pierwszej kolejności
1. 8-bajtowy MAC.
2. 4-bajtowy licznik anti-replay per-peer.
3. potwierdzenia `ACK`.
4. retransmisję z limitem prób.
5. zapisywanie liczników i trusted do NVM.
6. rozdzielenie formatów ramek użytkowych i parujących.

## 18.2. Co wdrożyć w drugiej kolejności
1. challenge 128-bit w parowaniu,
2. pseudonimy sesyjne,
3. rotację kluczy,
4. bardziej zaawansowany routing niż prosty flood relay,
5. politykę wygaszania starych peerów.

---

## 19. Proponowany plan realizacji projektu

## Etap 1 – komunikacja podstawowa
- uruchomienie łącza RFM95W pomiędzy Raspberry Pi i STM32,
- obsługa nadawania / odbioru,
- prosty format ramki,
- wyświetlanie wiadomości na ekranie,
- odpowiedzi przyciskami.

## Etap 2 – potwierdzenia i forwarding
- `ACK`,
- retransmisja po timeout,
- `TTL`,
- deduplikacja ramek,
- forwarding przez inne węzły.

## Etap 3 – bezpieczeństwo obecnej wersji
- integracja z TPM,
- lista trusted,
- szyfrowanie `USER`,
- bieżący `AuthTag`,
- zapis konfiguracji do EEPROM.

## Etap 4 – wzmocnienie bezpieczeństwa
- przejście na 64-bit MAC,
- challenge 128-bit,
- licznik anti-replay per-peer,
- re-key.

## Etap 5 – panel webowy
- interfejs po Wi-Fi,
- lista urządzeń,
- wysyłanie wiadomości,
- status dostarczenia,
- historia odpowiedzi.

---

## 20. Checklista rzeczy do wprowadzenia

### Funkcjonalność podstawowa
- [ ] zdefiniować finalny format ramki `USER_MSG`
- [ ] zdefiniować finalny format ramki `ACK`
- [ ] zdefiniować finalny format ramki `RESP`
- [ ] zdefiniować finalny format ramek `JOIN_REQ` i `JOIN_ACCEPT`
- [ ] wdrożyć obsługę `TTL`
- [ ] wdrożyć forwarding wiadomości
- [ ] wdrożyć deduplikację ramek
- [ ] wdrożyć retransmisję po braku `ACK`
- [ ] wdrożyć obsługę 3 przycisków i mapowanie odpowiedzi
- [ ] wdrożyć wyświetlanie wiadomości na ekranie
- [ ] wdrożyć status dostarczenia na Raspberry Pi

### Bezpieczeństwo
- [ ] zastąpić 4B `AuthTag` przez 8B MAC
- [ ] zdecydować: `HMAC-SHA256-64` czy `AES-CMAC-64`
- [ ] wdrożyć licznik anti-replay per-peer
- [ ] zapisywać stan liczników do NVM
- [ ] wdrożyć challenge 128-bit w parowaniu
- [ ] wymusić fizyczne potwierdzenie parowania przyciskiem
- [ ] dopracować sposób wyprowadzania klucza per-peer
- [ ] wdrożyć politykę rotacji kluczy
- [ ] ograniczyć liczbę jawnych metadanych
- [ ] rozważyć pseudonimy sesyjne zamiast stałych ID

### TPM / pamięć trwała
- [ ] dopracować wykorzystanie RNG z TPM
- [ ] określić, co dokładnie jest trzymane w EEPROM
- [ ] zabezpieczyć aktualizację rekordów trusted przed uszkodzeniem zasilania
- [ ] wdrożyć procedurę usuwania kluczy i trusted
- [ ] sprawdzić, czy reset urządzenia nie powoduje niespójności liczników

### Sieć i niezawodność
- [ ] ustalić politykę retransmisji
- [ ] dobrać wartości timeoutów
- [ ] dobrać domyślny `TTL`
- [ ] dodać losowe opóźnienie przed forwardingiem
- [ ] ograniczyć floodowanie przy wielu węzłach
- [ ] przetestować pracę w LoRa i FSK
- [ ] porównać zasięg, opóźnienie i odporność dla obu trybów

### Panel webowy
- [ ] przygotować prosty backend na Raspberry Pi
- [ ] przygotować formularz wysyłki wiadomości
- [ ] dodać listę urządzeń i ich statusów
- [ ] dodać historię wiadomości
- [ ] dodać podgląd `ACK`
- [ ] dodać podgląd odpowiedzi z przycisków

### Testy
- [ ] test poprawnego doręczenia
- [ ] test utraty pojedynczej ramki
- [ ] test retransmisji
- [ ] test multi-hop
- [ ] test duplicate frame
- [ ] test replay attack
- [ ] test błędnego MAC
- [ ] test nieautoryzowanego urządzenia
- [ ] test usuwania trusted
- [ ] test restartu urządzenia i odtwarzania stanu
- [ ] test zachowania po zaniku zasilania podczas zapisu NVM

---

## 21. Podsumowanie

Projekt stanowi bezpieczną, lekką sieć pagerową dla krótkich komunikatów tekstowych, w której Raspberry Pi Zero pełni rolę węzła zarządzającego z interfejsem webowym, a urządzenia STM32 z modułami RFM95W pełnią rolę odbiorników i przekaźników. Obecna wersja systemu posiada już podstawowe mechanizmy ochrony, takie jak szyfrowanie treści i weryfikacja tagu autentyczności, jednak wymaga dalszego wzmocnienia, szczególnie w obszarze MAC, anti-replay oraz procesu parowania.

Najważniejszym kompromisem projektowym jest limit 64 bajtów. Z tego powodu format ramki musi być bardzo zwarty, a funkcje bezpieczeństwa powinny być dobierane tak, aby zapewnić realną ochronę bez nadmiernego narzutu. Zaproponowana architektura pozwala osiągnąć ten cel i jednocześnie zachować prostotę wdrożenia na platformie STM32 + RFM95W + Raspberry Pi Zero.