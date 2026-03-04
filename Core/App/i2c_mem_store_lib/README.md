# i2c_mem_store_lib (M24C01-R profile)

Biblioteka pamięci zewnętrznej na I2C1 dla EEPROM ST M24C01-R (1 Kbit = 128 B) do:

- trwałego logowania odebranych wiadomości,
- przechowywania zaszyfrowanych kodów/rekordów dla bezpieczeństwa.

## Założenia sprzętowe (M24C01-R)

- pamięć na `I2C1` (`hi2c1`),
- typ: EEPROM M24C01-R z adresowaniem 8-bit (`I2C_MEMADD_SIZE_8BIT`),
- domyślny adres I2C: `0x50` (7-bit).

## Układ danych w pamięci (128 B)

- `0x0000` metadata primary (24 B),
- `0x0018` metadata mirror (24 B),
- `0x0030..` ring log wiadomości (sloty 24 B, payload do 6 B),
- końcówka pamięci: partycja `secret` (domyślnie 24 B, 1 slot 24 B, payload do 14 B).

Uwaga: M24C01-R ma bardzo małą pojemność, więc log wiadomości przechowuje tylko krótkie payloady.

## Bezpieczeństwo

W partycji `secret` dane są szyfrowane software'owo (XTEA-CTR, klucz 128-bit z konfiguracji).

Dla produkcji:

- nie trzymaj klucza na stałe w firmware,
- klucz ładuj/derywuj z TPM (np. NV + policy, albo sealed material),
- rotuj klucze i wersjonuj format rekordu.

## Szybki start

```c
#include "i2c_mem_store_lib/i2c_mem_store.h"

extern I2C_HandleTypeDef hi2c1;

static i2c_mem_store_t g_store;

void mem_init(void)
{
    i2c_mem_store_cfg_t cfg;
    i2c_mem_store_default_cfg_m24c01r(&cfg, &hi2c1);
    (void)i2c_mem_store_init(&g_store, &cfg, true);
}
```

## API kluczowe

- `i2c_mem_store_append_message(...)` – zapis odebranej wiadomości do logu,
- `i2c_mem_store_read_message(...)` – odczyt historii od najnowszej,
- `i2c_mem_store_secret_write/read(...)` – zapis/odczyt zaszyfrowanych danych,
- `i2c_mem_store_trusted_device_write/read(...)` – wygodny format rekordu „trusted device”.
