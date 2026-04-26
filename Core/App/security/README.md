# Security

Ten katalog zbiera założenia i zadania dla bezpieczeństwa komunikacji BEKO.

Dokumenty pomocnicze:
- `THREAT_MODEL.md` - skrócony model zagrożeń dla bieżącej architektury,
- `STRIDE_CIA_ANALYSIS.md` - tabela klas ataków, obecnych zabezpieczeń i dalszych mitigacji.

## Aktualny model

- `LAVIET_FRAME_V1` używa `src_id`, `dst_id`, `msg_id`, `counter`, `payload_len` i `mac_tag`.
- Ramki są uwierzytelniane HMAC-SHA256, a payload może być szyfrowany AES-CTR.
- Relay mesh/TTL został usunięty z bieżącej architektury; node działa w topologii gwiazdy z gatewayem.
- Ochrona przed replay w pierwszym wdrożeniu używa monotonicznego `counter` dla relacji gateway-node i zapisuje go w małym slocie `secret` NVM.
- `TPM PP` w obecnym hardware jest podłączony do samego modułu TPM, nie do GPIO MCU.
  Pin TPM PP to pin `7`, aktywny stanem `VDD`.
- TPM jest podłączony przez `I2C3` (`PC0` = SCL, `PC1` = SDA); reset `TPM_RESET#` jest na `PB0`, a `TPM_DAVINT#` / `PIRQ` na `PH0` jako `EXTI0`, aktywny niskim stanem.
- Firmware używa TPM jako źródła trwałego root seeda:
  - główny NV index: `0x01C10101`,
  - legacy read-only/migration index: `0x01C10100`,
  - atrybuty głównego indexu: `PPWRITE`, `OWNERREAD`, `NO_DA`.
- Zapis lub rotacja root seeda wymaga aktywnego TPM PP. STM32 nie odczytuje tego
  pinu jako GPIO; wymuszenie odbywa się przez TPM authorization (`TPM_RH_PLATFORM`
  dla `NV_Write`).
- Klucz szyfrowania sekretów EEPROM jest wyprowadzany z TPM-backed root seeda.
  Przy pierwszym starcie po zmianie firmware próbuje przepisać stare wpisy EEPROM
  z dawnego stałego klucza na klucz wyprowadzony z TPM.
- PP nie zabezpiecza jeszcze całego pairingu radiowego. Jeśli pairing albo inne
  operacje administracyjne mają wymagać fizycznej obecności, trzeba dodać osobne
  TPM policy/session albo równoważny flow oparty o TPM.

## DoS / Flood Defense

Poniżej są mechanizmy, które można wdrażać warstwowo. Nie trzeba robić wszystkiego naraz.

### 1. Rate limiting per source

- Trzymać licznik ramek na `src_id` w krótkim oknie czasu, np. `N / 10 s`.
- Po przekroczeniu limitu przestać obsługiwać ruch z tego źródła.
- Dla bardzo agresywnych źródeł wejść w czasowe `cooldown`.

### 2. Global TX budget

- Osobno limitować ruch użytkownika i ruch administracyjny.
- Węzeł nie powinien poświęcić całego duty-cycle na flood control traffic.
- Praktycznie: token bucket dla TX, np. osobny budżet na minutę.

### 3. Separate limits for control traffic

- `PAIR_REQ`, `PAIR_RESP`, `ERROR`, `CFG`, `COUNTER_SYNC`, `KEY_ROTATE` i `ACK` powinny mieć niższe limity niż zwykłe `DATA`.
- Pairing powinien działać tylko w krótkim oknie serwisowym.
- Powtarzane `PAIR_REQ` od jednego źródła powinny być szybko wyciszane.

### 4. Replay-aware abuse scoring

- Każde wykrycie replay zwiększa licznik nadużyć dla `src_id`.
- Po kilku replayach z rzędu można:
  - zablokować obsługę źródła,
  - obniżyć priorytet,
  - logować zdarzenie security.

### 5. Strict frame policy

- Przyjmować tylko ramki, które pasują do topologii gateway-node.
- Nie generować `ACK` dla broadcastu ani dla błędnego HMAC/replay.
- Walidować typ, flagi, `payload_len`, `src_id` i `dst_id` przed kryptografią.

### 6. Bounded parsing cost

- Odrzucać ramki z nieprawidłowym `src_id`, `dst_id`, `msg_id`, `counter` lub długością przed cięższą logiką.
- Nie robić drogich operacji kryptograficznych dla ruchu, który już wygląda na śmieciowy.
- Przy floodzie najtańsze filtry powinny działać jako pierwsze.

### 7. Queue and UI protection

- Oddzielić logi security od ścieżki krytycznej UI.
- Nie generować popupu dla każdego zdarzenia flood/replay.
- Zamiast tego agregować liczniki i wyświetlać skrócony status.

### 8. Channel occupancy protection

- Auto-ping i control traffic powinny respektować duty-cycle i własny budżet TX.
- W stanie przeciążenia najpierw ograniczać ruch administracyjny/testowy, dopiero potem ruch użytkownika.
- Dobrze działa też adaptacyjne wydłużanie odstępów TX po wykryciu floodu.

## Najbardziej opłacalna kolejność wdrożenia

1. Per-source rate limiting.
2. Global TX budget.
3. Abuse scoring dla replay/flood.
4. Ograniczenia pairing/control traffic.
5. Agregowane logowanie security zamiast popup spam.
