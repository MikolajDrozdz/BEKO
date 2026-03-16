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
