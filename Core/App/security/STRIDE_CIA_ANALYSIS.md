# STRIDE / CIA Analysis

Ten dokument porządkuje źródła ataku na system BEKO według dwóch perspektyw:
- `STRIDE`: klasy ataków na tożsamość, integralność, dostępność i kontrolę systemu,
- `CIA`: wpływ na poufność, integralność i dostępność.

Tabela ma trzy cele:
- pokazać, jakie klasy ataków są już częściowo adresowane,
- zaznaczyć, czego jeszcze brakuje w obecnym kodzie,
- dać listę praktycznych kierunków wdrożenia.

## STRIDE

| Typ ataku | Wdrożone rozwiązanie | Możliwy sposób rozwiązania / dalsze utwardzenie |
|---|---|---|
| `S` Spoofing: podszycie się pod inny węzeł przez fałszywy `src_id` | HMAC-SHA256 dla `LAVIET_FRAME_V1`, lokalny `node_id`, pairing/trust lista | Przenieść sekret bazowy do TPM/NV i dodać policy dla operacji administracyjnych |
| `S` Spoofing: podszycie się pod zaufane urządzenie podczas pairingu | `PAIR_REQ / PAIR_RESP`, zapis trusted devices, lokalna zgoda użytkownika | Uwierzytelnić pairing challenge-response, generować pair code z TPM RNG, dodać policy TPM dla zgody fizycznej |
| `T` Tampering: modyfikacja payloadu w locie | HMAC-SHA256 dla każdej ramki i AES-CTR dla payloadu szyfrowanego | Podmienić portable crypto backend na sprzętowy HAL CRYP/HASH/RNG |
| `T` Tampering: zmiana pól adresacji (`dst_id`, `msg_id`, `counter`) | Pola nagłówka są w HMAC, a parser waliduje typy/flagi/długości | Dodać pełną trwałość counterów i diagnostykę przyczyn odrzucenia |
| `T` Tampering: manipulacja stanem EEPROM / listą trusted | Częściowe formaty z CRC i wersjonowaniem danych | Dodać integralność rekordów security, licznik wersji, opcjonalnie sekret lub seed wyprowadzany z TPM |
| `R` Repudiation: brak możliwości wykazania, kto wysłał wiadomość | `src_id`, `msg_id`, log RX do store, logi terminala | Dodać trwalszy audit log security, powód odrzucenia, liczniki replay/bad-auth, znaczniki czasu lub monotoniczne liczniki |
| `R` Repudiation: brak dowodu akceptacji pairingu lub rotacji klucza | Logi runtime i menu popup | Zapisywać zdarzenia security w trwałym logu z typem operacji, `src_id`, wynikiem i przyczyną |
| `I` Information Disclosure: podsłuch payloadu `USER` | `coding` dla secure frames | Domyślnie wymusić secure mode dla ruchu użytkownika, rotować klucz sieciowy, użyć mocniejszej prymitywy niż obecny software cipher |
| `I` Information Disclosure: wyciek klucza z EEPROM/firmware | Seed i key są obecnie zarządzane w software, częściowo bootstrapowane przez TPM RNG | Trzymać sekret bazowy w TPM/NV lub sealing, ograniczyć obecność klucza w RAM, wyczyścić bufory po użyciu |
| `I` Information Disclosure: podgląd pair code lub danych trusted | Trusted store i menu urządzenia | Ukryć lub skrócić ekspozycję pair code na UI, nie logować pełnych sekretów na UART, ograniczyć odczyt danych trusted do trybu serwisowego |
| `D` Denial of Service: replay powodujący lokalne akcje | Drop ramek gateway->node ze starym `counter` | Dodać abuse score per source, cooldown, agregację alertów security i liczniki replay drop |
| `D` Denial of Service: flood `DATA` i broadcastów | Broadcast tylko od gatewaya, brak ACK dla broadcastu, duty-cycle dla auto ping | Per-source rate limiting, globalny budżet TX i limity dla ruchu control |
| `D` Denial of Service: flood ruchem control / pairing | Pairing jest rozdzielony typami ramek | Dodać osobny rate limiting dla `PAIR_*`, krótkie okno serwisowe, lokalną zgodę TPM/PP dla akceptacji |
| `D` Denial of Service: zapchanie UI i kolejek komunikatami security | Częściowa ochrona kolejek UI/LCD, ograniczenie popupów w menu | Osobna kolejka security, agregowanie zdarzeń flood, nie generować popupu dla każdego błędu/replay |
| `D` Denial of Service: zawieszanie backendu radiowego przez nietypowe TX/FSK | Watchdog TX, recovery ścieżki radiowej, limity payload dla FSK/OOK | Dodać licznik recoveries, auto-disable wadliwego trybu po serii błędów, twarde ograniczenia konfiguracji backendu |
| `E` Elevation of Privilege: zdalne wykonanie operacji administracyjnej | Część operacji wymaga wejścia do odpowiedniego menu/flow | Rozdzielić role: user/admin/service, chronić operacje security przez policy TPM, local presence i osobne komendy admin |
| `E` Elevation of Privilege: przejęcie pairingu bez fizycznej obecności | Brak pełnej ochrony fizycznej w obecnej implementacji | Wdrożyć TPM `PolicyPhysicalPresence`, policy session i ograniczenie pairingu do lokalnie autoryzowanego okna |
| `E` Elevation of Privilege: użycie starych ramek sterujących do wymuszenia stanu | Replay protection na `counter` | Dodać pełną trwałość counterów oraz oddzielne polityki dla `CFG`, `COUNTER_SYNC` i `KEY_ROTATE` |

## CIA

| Obszar CIA | Źródło ataku | Wdrożone rozwiązanie | Możliwy sposób rozwiązania / dalsze utwardzenie |
|---|---|---|---|
| `C` Confidentiality | Podsłuch radiowy `USER` | `coding` dla secure ramek | Wymusić secure-by-default, mocniejsza kryptografia, regularna rotacja klucza sieciowego |
| `C` Confidentiality | Odczyt sekretów z pamięci trwałej | Częściowe formaty i porządek store | Sealed storage z TPM, NV index z policy, ograniczenie debug/logów sekretów |
| `C` Confidentiality | Wyciek przez UART/logi | Terminal pokazuje szczegóły TX/RX | Dodać tryb produkcyjny bez pełnych dumpów, maskować wrażliwe dane i pair code |
| `I` Integrity | Modyfikacja ramek w locie | HMAC-SHA256 dla `LAVIET_FRAME_V1` | Sprzętowy HASH i dokładniejsza telemetryka bad-auth |
| `I` Integrity | Replay starych ramek | Monotoniczny `counter` gateway-node | Trwały zapis odporny na rollback i telemetryka security |
| `I` Integrity | Fałszywe trusted device / nieautoryzowane parowanie | Pairing flow i trusted list | Pairing challenge-response, TPM RNG, lokalna zgoda fizyczna przez policy TPM |
| `I` Integrity | Fałszywa konfiguracja security po restarcie | Store versioning i częściowe CRC | Ochrona integralności rekordów security, monotoniczny counter, binding do TPM |
| `A` Availability | Flood radiowy | Brak mesh relay, replay drop, broadcast bez ACK | Token bucket per source, globalny budżet TX, adaptive backoff |
| `A` Availability | Zużycie duty-cycle / baterii | Auto ping respektuje duty-cycle | Budżet TX dla control traffic i automatyczne obniżanie priorytetu flood sources |
| `A` Availability | Zawieszenie toru radiowego lub backendu | Recovery watchdog i reset ścieżki radiowej | Eskalacja recovery, licznik awarii per modulacja, bezpieczny fallback do LoRa preset |
| `A` Availability | Zablokowanie UI/obsługi operatora przez spam zdarzeń | Częściowe utwardzenie UI i kolejek | Osobna ścieżka alertów security, status zbiorczy zamiast popup spam |

## Najważniejsze priorytety

1. Przenieść crypto backend na sprzętowy HAL CRYP/HASH/RNG.
2. Dodać per-source rate limiting i globalny budżet TX.
3. Zaimplementować prawdziwą ochronę fizycznej zgody przez TPM policy, nie przez zwykły GPIO.
4. Ograniczyć ekspozycję sekretów w EEPROM, RAM i logach UART.
5. Dodać trwałą telemetrykę security: `replay_drop`, `bad_auth`, `duplicate_drop`, `recovery_count`.
