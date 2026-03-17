# Security

Ten katalog zbiera założenia i zadania dla bezpieczeństwa komunikacji BEKO.

Dokumenty pomocnicze:
- `THREAT_MODEL.md` - skrócony model zagrożeń dla bieżącej architektury,
- `STRIDE_CIA_ANALYSIS.md` - tabela klas ataków, obecnych zabezpieczeń i dalszych mitigacji.

## Aktualny model

- `BEKO_NET_V1` używa `src_id`, `dst_id`, `msg_id` i `ttl`.
- Ramki `USER` mogą być szyfrowane i uwierzytelniane przez `coding + auth tag`.
- Relay działa tylko dla ramek `USER`, które nie są do mnie i mają jeszcze `ttl > 1`.
- Ochrona przed replay jest realizowana per `src_id` przez przesuwne okno `msg_id`.
- `TPM PP` w obecnym hardware jest podłączony do samego modułu TPM, nie do GPIO MCU.
  To znaczy, że nie wolno traktować go jak zwykłego przycisku aplikacji. Jeśli ma
  sterować pairingiem albo operacjami administracyjnymi, trzeba użyć polityk TPM
  (`PolicyPhysicalPresence`, policy session, ewentualnie NV/object auth), a nie
  lokalnego odczytu stanu pinu przez STM32.

## DoS / Flood Defense

Poniżej są mechanizmy, które można wdrażać warstwowo. Nie trzeba robić wszystkiego naraz.

### 1. Rate limiting per source

- Trzymać licznik ramek na `src_id` w krótkim oknie czasu, np. `N / 10 s`.
- Po przekroczeniu limitu przestać relayować ruch z tego źródła.
- Dla bardzo agresywnych źródeł wejść w czasowe `cooldown`.

### 2. Global relay budget

- Osobno limitować ruch lokalny i ruch relayowany.
- Węzeł nie powinien poświęcić całego duty-cycle tylko na cudze pakiety.
- Praktycznie: token bucket dla `forward`, np. osobny budżet na minutę.

### 3. Separate limits for control traffic

- `JOIN_REQ`, `JOIN_ACCEPT`, `JOIN_REJECT`, `TRUST_REMOVED`, `ACK` powinny mieć niższe limity niż zwykłe `USER`.
- Pairing powinien działać tylko w krótkim oknie serwisowym.
- Powtarzane `JOIN_REQ` od jednego źródła powinny być szybko wyciszane.

### 4. Replay-aware abuse scoring

- Każde wykrycie replay zwiększa licznik nadużyć dla `src_id`.
- Po kilku replayach z rzędu można:
  - zablokować relay,
  - obniżyć priorytet,
  - logować zdarzenie security.

### 5. Strict forwarding policy

- Forwardować tylko ramki, które naprawdę mają sens sieciowo.
- Nie relayować ramek sterujących i lokalnych komunikatów administracyjnych.
- Ograniczyć maksymalny `ttl` już przy dekodowaniu.

### 6. Bounded parsing cost

- Odrzucać ramki z nieprawidłowym `src_id`, `msg_id`, `ttl` lub długością przed cięższą logiką.
- Nie robić drogich operacji kryptograficznych dla ruchu, który już wygląda na śmieciowy.
- Przy floodzie najtańsze filtry powinny działać jako pierwsze.

### 7. Queue and UI protection

- Oddzielić logi security od ścieżki krytycznej UI.
- Nie generować popupu dla każdego zdarzenia flood/replay.
- Zamiast tego agregować liczniki i wyświetlać skrócony status.

### 8. Channel occupancy protection

- Relay i auto-ping powinny respektować duty-cycle i własny budżet TX.
- W stanie przeciążenia najpierw wyłączać relay, dopiero potem mniej ważny ruch lokalny.
- Dobrze działa też adaptacyjne wydłużanie odstępów TX po wykryciu floodu.

## Najbardziej opłacalna kolejność wdrożenia

1. Per-source rate limiting.
2. Global relay budget.
3. Abuse scoring dla replay/flood.
4. Ograniczenia pairing/control traffic.
5. Agregowane logowanie security zamiast popup spam.
