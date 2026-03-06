# st33ktpm2x_lib

Biblioteka dla ST33KTPM2X (TPM 2.0) po I2C (TIS/FIFO) pod STM32 HAL.

## Założenia

- Interfejs TPM: `I2C3` (`hi2c3`).
- Pin reset TPM: `PB0` (`TMP_RESET_Pin`/`TMP_RESET_GPIO_Port` z `main.h`).
- Opcjonalny przycisk Physical Presence (PP): konfigurowany w `st33ktpm2x_cfg_t`.

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
- wsparcie dla PP:
  - odczyt stanu przycisku,
  - czekanie na naciśnięcie.
