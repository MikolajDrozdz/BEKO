/**
 * @file st33ktpm2x.h
 * @brief High-level and transport-level driver for ST33KTPM2X over I2C (TIS/FIFO).
 *
 * This module provides:
 * - low-level TPM TIS over I2C access,
 * - locality handling,
 * - command/response transport,
 * - selected TPM2 helper commands for security feature development.
 *
 * The implementation is designed to be extensible. For advanced commands
 * (sessions, HMAC, policy, NV authorization), use raw command transport APIs
 * and add dedicated marshaling helpers in your application layer.
 */

#ifndef APP_ST33KTPM2X_LIB_ST33KTPM2X_H_
#define APP_ST33KTPM2X_LIB_ST33KTPM2X_H_

#include "stm32u5xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @name Default Configuration
 *  @{
 */
#define ST33KTPM2X_I2C_ADDR_DEFAULT        0x2EU  /**< Default 7-bit TPM I2C address. */
#define ST33KTPM2X_CMD_MAX_BYTES           1024U /**< Safe default max command size.  */
#define ST33KTPM2X_RSP_MAX_BYTES           1024U /**< Safe default max response size. */
/** @} */

/** @name TPM2 Common Constants
 *  @{
 */
#define ST33KTPM2X_TPM2_ST_NO_SESSIONS     0x8001U
#define ST33KTPM2X_TPM2_ST_SESSIONS        0x8002U
#define ST33KTPM2X_TPM2_RC_SUCCESS         0x00000000UL

#define ST33KTPM2X_TPM2_CC_STARTUP         0x00000144UL
#define ST33KTPM2X_TPM2_CC_SELF_TEST       0x00000143UL
#define ST33KTPM2X_TPM2_CC_GET_RANDOM      0x0000017BUL
#define ST33KTPM2X_TPM2_CC_GET_CAPABILITY  0x0000017AUL
#define ST33KTPM2X_TPM2_CC_PCR_READ        0x0000017EUL

#define ST33KTPM2X_TPM2_SU_CLEAR           0x0000U
#define ST33KTPM2X_TPM2_SU_STATE           0x0001U

#define ST33KTPM2X_TPM2_ALG_SHA256         0x000BU

#define ST33KTPM2X_TPM2_CAP_ALGS           0x00000000UL
#define ST33KTPM2X_TPM2_CAP_HANDLES        0x00000001UL
#define ST33KTPM2X_TPM2_CAP_COMMANDS       0x00000002UL
#define ST33KTPM2X_TPM2_CAP_PCRS           0x00000005UL
#define ST33KTPM2X_TPM2_CAP_TPM_PROPERTIES 0x00000006UL
/** @} */

/**
 * @brief Driver status/result codes.
 */
typedef enum
{
    ST33KTPM2X_OK = 0,         /**< Operation completed successfully. */
    ST33KTPM2X_EINVAL,         /**< Invalid argument. */
    ST33KTPM2X_ESTATE,         /**< Invalid driver state. */
    ST33KTPM2X_EHAL,           /**< HAL I2C or GPIO operation failed. */
    ST33KTPM2X_ETIMEOUT,       /**< Timeout while waiting for TPM state. */
    ST33KTPM2X_EOVERFLOW,      /**< Provided buffer too small for response. */
    ST33KTPM2X_EPROTO,         /**< TPM protocol/TIS response format error. */
    ST33KTPM2X_ETPM_RC,        /**< TPM responded with non-success TPM2_RC. */
    ST33KTPM2X_ENOTSUP         /**< Optional feature not configured/supported. */
} st33ktpm2x_status_t;

/**
 * @brief Runtime configuration for ST33KTPM2X instance.
 */
typedef struct
{
    I2C_HandleTypeDef *hi2c;       /**< I2C handle (use I2C3 in this project). */
    uint16_t i2c_addr_7bit;        /**< 7-bit I2C address of TPM. */

    GPIO_TypeDef *reset_port;      /**< Reset GPIO port (PB0 in your setup). */
    uint16_t reset_pin;            /**< Reset GPIO pin. */
    uint32_t reset_pulse_ms;       /**< Reset low pulse width. */
    uint32_t reset_recovery_ms;    /**< Delay after reset release. */

    GPIO_TypeDef *pp_port;         /**< Optional Physical Presence button port. */
    uint16_t pp_pin;               /**< Optional Physical Presence button pin. */
    GPIO_PinState pp_active_state; /**< Logical active level for PP button. */

    uint32_t io_timeout_ms;        /**< I2C transfer timeout. */
    uint32_t locality_timeout_ms;  /**< Timeout for locality acquisition. */
    uint32_t burst_timeout_ms;     /**< Timeout for burst/data availability. */
} st33ktpm2x_cfg_t;

/**
 * @brief Driver context for one TPM device.
 */
typedef struct
{
    st33ktpm2x_cfg_t cfg;      /**< Active runtime configuration. */
    bool initialized;          /**< Driver init state. */
    bool locality0_acquired;   /**< Locality 0 acquisition state. */
} st33ktpm2x_t;

/**
 * @brief Fill config with safe defaults (including PB0 reset pin defaults from `main.h`).
 * @param cfg [out] Configuration object to initialize.
 * @param hi2c I2C peripheral handle (use `&hi2c3`).
 */
void st33ktpm2x_default_cfg(st33ktpm2x_cfg_t *cfg, I2C_HandleTypeDef *hi2c);

/**
 * @brief Initialize TPM driver context.
 * @param ctx Driver context.
 * @param cfg Runtime configuration.
 * @return Status code.
 */
st33ktpm2x_status_t st33ktpm2x_init(st33ktpm2x_t *ctx, const st33ktpm2x_cfg_t *cfg);

/**
 * @brief Deinitialize context and release locality if needed.
 * @param ctx Driver context.
 * @return Status code.
 */
st33ktpm2x_status_t st33ktpm2x_deinit(st33ktpm2x_t *ctx);

/**
 * @brief Perform hardware reset pulse on TPM reset pin.
 * @param ctx Driver context.
 * @return Status code.
 */
st33ktpm2x_status_t st33ktpm2x_hard_reset(st33ktpm2x_t *ctx);

/**
 * @brief Read state of optional Physical Presence button.
 * @param ctx Driver context.
 * @param pressed [out] `true` if button is active.
 * @return Status code (`ST33KTPM2X_ENOTSUP` if PP pin is not configured).
 */
st33ktpm2x_status_t st33ktpm2x_pp_is_pressed(st33ktpm2x_t *ctx, bool *pressed);

/**
 * @brief Wait until Physical Presence button becomes active.
 * @param ctx Driver context.
 * @param timeout_ms Timeout in ms.
 * @return Status code.
 */
st33ktpm2x_status_t st33ktpm2x_pp_wait_pressed(st33ktpm2x_t *ctx, uint32_t timeout_ms);

/**
 * @brief Acquire TPM locality 0.
 * @param ctx Driver context.
 * @return Status code.
 */
st33ktpm2x_status_t st33ktpm2x_request_locality0(st33ktpm2x_t *ctx);

/**
 * @brief Release TPM locality 0.
 * @param ctx Driver context.
 * @return Status code.
 */
st33ktpm2x_status_t st33ktpm2x_release_locality0(st33ktpm2x_t *ctx);

/**
 * @brief Read TPM DID_VID and RID registers.
 * @param ctx Driver context.
 * @param did_vid [out] 32-bit DID_VID register value.
 * @param rid [out] 8-bit Revision ID value.
 * @return Status code.
 */
st33ktpm2x_status_t st33ktpm2x_read_identity(st33ktpm2x_t *ctx, uint32_t *did_vid, uint8_t *rid);

/**
 * @brief Send one raw TPM command and receive full response.
 *
 * This is the primary extension point for advanced TPM2 features.
 *
 * @param ctx Driver context.
 * @param command Command buffer (TPM2 packet, complete header+payload).
 * @param command_len Command length in bytes.
 * @param response Buffer for response packet.
 * @param response_capacity Response buffer capacity in bytes.
 * @param response_len [out] Actual response length.
 * @param tpm_rc [out] TPM2 response code parsed from response header.
 * @return Status code.
 */
st33ktpm2x_status_t st33ktpm2x_transceive(st33ktpm2x_t *ctx,
                                          const uint8_t *command,
                                          uint16_t command_len,
                                          uint8_t *response,
                                          uint16_t response_capacity,
                                          uint16_t *response_len,
                                          uint32_t *tpm_rc);

/**
 * @brief Execute TPM2_Startup.
 * @param ctx Driver context.
 * @param startup_type Use `ST33KTPM2X_TPM2_SU_CLEAR` or `ST33KTPM2X_TPM2_SU_STATE`.
 * @param tpm_rc [out] TPM response code.
 * @return Status code.
 */
st33ktpm2x_status_t st33ktpm2x_tpm2_startup(st33ktpm2x_t *ctx,
                                            uint16_t startup_type,
                                            uint32_t *tpm_rc);

/**
 * @brief Execute TPM2_SelfTest.
 * @param ctx Driver context.
 * @param full_test `true` for full self-test, `false` for incremental self-test.
 * @param tpm_rc [out] TPM response code.
 * @return Status code.
 */
st33ktpm2x_status_t st33ktpm2x_tpm2_self_test(st33ktpm2x_t *ctx,
                                              bool full_test,
                                              uint32_t *tpm_rc);

/**
 * @brief Execute TPM2_GetRandom.
 * @param ctx Driver context.
 * @param requested_bytes Number of random bytes requested from TPM.
 * @param random_out Output buffer for random bytes.
 * @param random_capacity Capacity of `random_out`.
 * @param random_len [out] Actual bytes returned.
 * @param tpm_rc [out] TPM response code.
 * @return Status code.
 */
st33ktpm2x_status_t st33ktpm2x_tpm2_get_random(st33ktpm2x_t *ctx,
                                               uint16_t requested_bytes,
                                               uint8_t *random_out,
                                               uint16_t random_capacity,
                                               uint16_t *random_len,
                                               uint32_t *tpm_rc);

/**
 * @brief Execute TPM2_GetCapability and return raw capability payload.
 * @param ctx Driver context.
 * @param capability TPM capability selector.
 * @param property Capability-specific property/offset.
 * @param property_count Number of properties requested.
 * @param payload_out Raw capability payload output (without `moreData` byte).
 * @param payload_capacity Capacity of `payload_out`.
 * @param payload_len [out] Returned payload length.
 * @param more_data [out] `true` if TPM reports more data available.
 * @param tpm_rc [out] TPM response code.
 * @return Status code.
 */
st33ktpm2x_status_t st33ktpm2x_tpm2_get_capability(st33ktpm2x_t *ctx,
                                                   uint32_t capability,
                                                   uint32_t property,
                                                   uint32_t property_count,
                                                   uint8_t *payload_out,
                                                   uint16_t payload_capacity,
                                                   uint16_t *payload_len,
                                                   bool *more_data,
                                                   uint32_t *tpm_rc);

/**
 * @brief Execute TPM2_PCR_Read for one SHA-256 PCR index.
 * @param ctx Driver context.
 * @param pcr_index PCR index in range 0..23.
 * @param digest_sha256_out [out] 32-byte SHA-256 PCR digest.
 * @param update_counter_out [out] PCR update counter.
 * @param tpm_rc [out] TPM response code.
 * @return Status code.
 */
st33ktpm2x_status_t st33ktpm2x_tpm2_pcr_read_sha256(st33ktpm2x_t *ctx,
                                                    uint8_t pcr_index,
                                                    uint8_t digest_sha256_out[32],
                                                    uint32_t *update_counter_out,
                                                    uint32_t *tpm_rc);

/**
 * @brief Start TPM2 policy/auth session helper (scaffold API).
 * @param ctx Driver context.
 * @param session_handle_out [out] Created session handle.
 * @param tpm_rc [out] TPM response code.
 * @return Status code.
 */
st33ktpm2x_status_t st33ktpm2x_tpm2_start_auth_session(st33ktpm2x_t *ctx,
                                                        uint32_t *session_handle_out,
                                                        uint32_t *tpm_rc);

/**
 * @brief Apply PolicyPCR in an existing policy session (scaffold API).
 * @param ctx Driver context.
 * @param session_handle Policy session handle.
 * @param pcr_index PCR index.
 * @param tpm_rc [out] TPM response code.
 * @return Status code.
 */
st33ktpm2x_status_t st33ktpm2x_tpm2_policy_pcr(st33ktpm2x_t *ctx,
                                                uint32_t session_handle,
                                                uint8_t pcr_index,
                                                uint32_t *tpm_rc);

/**
 * @brief Apply Physical Presence policy branch (scaffold API).
 * @param ctx Driver context.
 * @param session_handle Policy session handle.
 * @param tpm_rc [out] TPM response code.
 * @return Status code.
 */
st33ktpm2x_status_t st33ktpm2x_tpm2_policy_physical_presence(st33ktpm2x_t *ctx,
                                                              uint32_t session_handle,
                                                              uint32_t *tpm_rc);

/**
 * @brief Apply PolicyCommandCode in an existing policy session (scaffold API).
 * @param ctx Driver context.
 * @param session_handle Policy session handle.
 * @param command_code TPM command code to authorize.
 * @param tpm_rc [out] TPM response code.
 * @return Status code.
 */
st33ktpm2x_status_t st33ktpm2x_tpm2_policy_command_code(st33ktpm2x_t *ctx,
                                                         uint32_t session_handle,
                                                         uint32_t command_code,
                                                         uint32_t *tpm_rc);

/**
 * @brief Apply PolicyOR branch combiner (scaffold API).
 * @param ctx Driver context.
 * @param session_handle Policy session handle.
 * @param digest_list Pointer to concatenated digest list.
 * @param digest_count Number of digests.
 * @param digest_size Single digest size.
 * @param tpm_rc [out] TPM response code.
 * @return Status code.
 */
st33ktpm2x_status_t st33ktpm2x_tpm2_policy_or(st33ktpm2x_t *ctx,
                                               uint32_t session_handle,
                                               const uint8_t *digest_list,
                                               uint8_t digest_count,
                                               uint8_t digest_size,
                                               uint32_t *tpm_rc);

/**
 * @brief Define NV index (scaffold API).
 * @param ctx Driver context.
 * @param nv_index NV index handle.
 * @param data_size NV data size.
 * @param attributes NV attributes mask.
 * @param tpm_rc [out] TPM response code.
 * @return Status code.
 */
st33ktpm2x_status_t st33ktpm2x_tpm2_nv_define(st33ktpm2x_t *ctx,
                                              uint32_t nv_index,
                                              uint16_t data_size,
                                              uint32_t attributes,
                                              uint32_t *tpm_rc);

/**
 * @brief Read bytes from NV index (scaffold API).
 * @param ctx Driver context.
 * @param nv_index NV index handle.
 * @param offset Offset in bytes.
 * @param out_data Output buffer.
 * @param out_capacity Output capacity.
 * @param out_len [out] Returned data length.
 * @param tpm_rc [out] TPM response code.
 * @return Status code.
 */
st33ktpm2x_status_t st33ktpm2x_tpm2_nv_read(st33ktpm2x_t *ctx,
                                            uint32_t nv_index,
                                            uint16_t offset,
                                            uint8_t *out_data,
                                            uint16_t out_capacity,
                                            uint16_t *out_len,
                                            uint32_t *tpm_rc);

/**
 * @brief Write bytes to NV index (scaffold API).
 * @param ctx Driver context.
 * @param nv_index NV index handle.
 * @param offset Offset in bytes.
 * @param data Input data.
 * @param data_len Input data length.
 * @param tpm_rc [out] TPM response code.
 * @return Status code.
 */
st33ktpm2x_status_t st33ktpm2x_tpm2_nv_write(st33ktpm2x_t *ctx,
                                             uint32_t nv_index,
                                             uint16_t offset,
                                             const uint8_t *data,
                                             uint16_t data_len,
                                             uint32_t *tpm_rc);

/**
 * @brief Flush transient context/session handle (scaffold API).
 * @param ctx Driver context.
 * @param handle Transient handle.
 * @param tpm_rc [out] TPM response code.
 * @return Status code.
 */
st33ktpm2x_status_t st33ktpm2x_tpm2_flush_context(st33ktpm2x_t *ctx,
                                                  uint32_t handle,
                                                  uint32_t *tpm_rc);

#ifdef __cplusplus
}
#endif

#endif /* APP_ST33KTPM2X_LIB_ST33KTPM2X_H_ */
