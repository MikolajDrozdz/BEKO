# Security
## Pytania 

- Jakie są wymagania dla odporności na replay?
- Czy każda ramka `USER` ma wymagać auth i szyfrowania?
- Jak wygląda odzyskiwanie zaufania po rotacji klucza?
- Czy tryb serwisowy ma mieć osobne uprawnienia i osobny klucz?

## Obszary

- Radio link LoRa/FSK/OOK
- Pairing nowych urządzeń
- Magazyn EEPROM
- Integracja z TPM

# Security TODO

- Spisać aktorów i poziomy zaufania.
- Zmapować dane wrażliwe i ich cykl życia.
- Zdefiniować minimalny zestaw testów bezpieczeństwa.

- Opisać dokładny threat model dla komunikacji lokalnej i radiowej.
- Rozdzielić warstwę policy/security od aktualnej logiki runtime w `security_main`.
- Uporządkować format przechowywania kluczy i konfiguracji w EEPROM.
- Zdefiniować wersjonowanie protokołu secure frame.
- Rozszerzyć dokumentację pairing i trust removal.
- Dodać testy regresyjne dla auth tag, coding i migracji EEPROM.

## DoS / Replay
*Co można dodać*
- Dodać per-source rate limiting dla `USER`.
- Dodać osobny rate limiting dla ramek control/pairing.
- Dodać globalny budżet relay TX niezależny od local TX.
- Dodać licznik nadużyć (`replay`, `bad auth`, `unknown src`) i czasowy cooldown.
- Agregować alerty security zamiast popupu dla każdego zdarzenia flood.
- Rozważyć telemetrykę security: liczba replay drop, relay drop, throttle hit.
