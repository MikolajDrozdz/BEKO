# Security
## Pytania 

- Jakie są wymagania dla odporności na replay?
- Czy każda ramka `OPERATOR` ma wymagać auth i szyfrowania?
- Jak wygląda odzyskiwanie zaufania po rotacji klucza?
- Czy tryb serwisowy ma mieć osobne uprawnienia i osobny klucz?

## Obszary

- Radio link LoRa/FSK/OOK
- Pairing nowych urządzeń
- Magazyn EEPROM
- Integracja z TPM i TPM PP

# Security TODO

- Spisać aktorów i poziomy zaufania.
- Zmapować dane wrażliwe i ich cykl życia.
- Zdefiniować minimalny zestaw testów bezpieczeństwa.

- Opisać dokładny threat model dla komunikacji lokalnej i radiowej.
- Rozdzielić warstwę policy/security od aktualnej logiki runtime w `security_main`.
- Dokończyć testy migracji EEPROM po przejściu na klucz wyprowadzany z TPM.
- Zdefiniować wersjonowanie protokołu secure frame.
- Rozszerzyć dokumentację pairing i trust removal.
- Dodać testy regresyjne dla auth tag, coding i migracji EEPROM.
- Przetestować restart z root seedem w TPM NV `0x01C10101`.
- Przetestować ścieżkę migracji z legacy TPM NV `0x01C10100`.
- Rozszerzyć TPM PP/policy z lokalnej rotacji root seeda na pairing i operacje admin.

## DoS / Replay
*Co można dodać*
- Dodać per-source rate limiting dla `OPERATOR`.
- Dodać osobny rate limiting dla ramek control/pairing.
- Dodać globalny budżet TX niezależny od local TX.
- Dodać licznik nadużyć (`replay`, `bad auth`, `unknown src`) i czasowy cooldown.
- Agregować alerty security zamiast popupu dla każdego zdarzenia flood.
- Rozważyć telemetrykę security: liczba replay drop, duplicate drop, throttle hit.
