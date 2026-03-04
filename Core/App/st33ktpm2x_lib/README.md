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

## Szybki start

```c
#include "st33ktpm2x_lib/st33ktpm2x.h"

extern I2C_HandleTypeDef hi2c3;

static st33ktpm2x_t tpm;

void tpm_example_init(void)
{
    st33ktpm2x_cfg_t cfg;
    uint32_t tpm_rc = 0;

    st33ktpm2x_default_cfg(&cfg, &hi2c3);

    /* Jeśli masz fizyczny przycisk PP, ustaw jego pin: */
    /* cfg.pp_port = GPIOx; */
    /* cfg.pp_pin = GPIO_PIN_y; */
    /* cfg.pp_active_state = GPIO_PIN_RESET; // albo SET */

    (void)st33ktpm2x_init(&tpm, &cfg);
    (void)st33ktpm2x_hard_reset(&tpm);
    (void)st33ktpm2x_tpm2_startup(&tpm, ST33KTPM2X_TPM2_SU_CLEAR, &tpm_rc);
    (void)st33ktpm2x_tpm2_self_test(&tpm, true, &tpm_rc);
}
```

## Rozwój bezpieczeństwa

Do bardziej zaawansowanych funkcji (sesje, NV z autoryzacją, HMAC/policy, attestation)
użyj:

- `st33ktpm2x_transceive(...)`

i buduj komendy TPM2 w warstwie wyżej (marshal/unmarshal).
