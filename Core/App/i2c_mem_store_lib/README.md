# i2c_mem_store_lib

Biblioteka pamięci zewnętrznej na I2C1 do:

- trwałego logowania odebranych wiadomości,
- przechowywania zaszyfrowanych kodów/rekordów dla bezpieczeństwa.

## Założenia sprzętowe

- pamięć na `I2C1` (`hi2c1`),
- typ: EEPROM/FRAM z adresowaniem 16-bit (`I2C_MEMADD_SIZE_16BIT` domyślnie),
- domyślny adres I2C: `0x50` (7-bit).

## Układ danych w pamięci

- `0x0000` metadata (primary + mirror),
- od `0x0100` ring log wiadomości (sloty 128 B),
- końcowa część pamięci: partycja `secret` (domyślnie 4096 B, sloty 96 B).

## Bezpieczeństwo

W partycji `secret` dane są szyfrowane software'owo (XTEA-CTR, klucz 128-bit z konfiguracji).

Dla produkcji:

- nie trzymaj klucza na stałe w firmware,
- klucz ładuj/derywuj z TPM (np. NV + policy, albo sealed material),
- rotuj klucze i wersjonuj format rekordu.
