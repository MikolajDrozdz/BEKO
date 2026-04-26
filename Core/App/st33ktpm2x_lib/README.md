# st33ktpm2x_lib

Biblioteka dla ST33KTPM2X (TPM 2.0) po I2C (TIS/FIFO) pod STM32 HAL.

## Założenia

- Interfejs TPM: `I2C3` (`hi2c3`).
- Pin reset TPM: `PB0` (`TMP_RESET_Pin`/`TMP_RESET_GPIO_Port` z `main.h`).
- Jeśli PP jest wyprowadzony do STM32 GPIO, może być konfigurowany w `st33ktpm2x_cfg_t`.
- W aktualnym hardware BEKO PP jest podłączony do samego TPM: pin `7`, aktywny stanem `VDD`.
  Tego pinu nie czyta STM32 jako zwykłego przycisku; jest używany przez TPM przy
  komendach wymagających fizycznej obecności.

## Co zawiera

- transport TPM TIS over I2C:
  - lokalność `locality 0`,
  - FIFO TX/RX,
  - parsowanie nagłówka TPM2 i `TPM_RC`.
- funkcje bazowe TPM2:
  - `TPM2_Startup`,
  - `TPM2_SelfTest`,
  - `TPM2_GetRandom`,
  - `TPM2_GetCapability`,
  - `TPM2_PCR_Read` (SHA-256, pojedynczy PCR).
- funkcje NV:
  - `TPM2_NV_DefineSpace`,
  - `TPM2_NV_Read`,
  - `TPM2_NV_Write` z owner authorization,
  - `TPM2_NV_Write` z platform authorization dla PP-protected NV.
- wsparcie dla PP jako GPIO, gdy hardware tak je podłączy:
  - odczyt stanu przycisku,
  - czekanie na naciśnięcie.

## Użycie w BEKO

`security_main` używa biblioteki TPM do:

- inicjalizacji TPM przed EEPROM,
- pobierania losowości z `TPM2_GetRandom`,
- przechowywania root seeda w NV indexie `0x01C10101`,
- migracyjnego odczytu starego NV indexu `0x01C10100`,
- wymuszenia aktywnego PP przy zapisie root seeda przez `NV_Write` z `TPM_RH_PLATFORM`.

Root seed nie powinien być już zapisywany jawnie do EEPROM, gdy TPM jest gotowy.
EEPROM przechowuje wpisy trusted/countery zaszyfrowane kluczem wyprowadzonym z
TPM-backed root seeda.
