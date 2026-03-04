/**
 * @file i2c_mem_store.h
 * @brief I2C NVM storage library for received messages and encrypted service codes.
 *
 * This library targets external I2C memory (EEPROM/FRAM family) connected to I2C1.
 * It provides:
 * - append-only ring log for received radio messages,
 * - dedicated encrypted partition for trusted-device/service codes.
 *
 * Encryption uses XTEA-CTR software cipher with a 128-bit key from configuration.
 * For production security, derive/load this key from TPM-protected material.
 */

#ifndef APP_I2C_MEM_STORE_LIB_I2C_MEM_STORE_H_
#define APP_I2C_MEM_STORE_LIB_I2C_MEM_STORE_H_

#include "stm32u5xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @name Defaults
 *  @{
 */
#define I2C_MEM_STORE_I2C_ADDR_DEFAULT            0x50U
#define I2C_MEM_STORE_TOTAL_BYTES_DEFAULT         128U
#define I2C_MEM_STORE_PAGE_BYTES_DEFAULT          8U
#define I2C_MEM_STORE_WRITE_CYCLE_MS_DEFAULT      5U
#define I2C_MEM_STORE_SECRET_AREA_BYTES_DEFAULT   24U
#define I2C_MEM_STORE_LOG_RECORD_SIZE             24U
#define I2C_MEM_STORE_LOG_PAYLOAD_MAX             6U
#define I2C_MEM_STORE_SECRET_SLOT_SIZE            24U
#define I2C_MEM_STORE_SECRET_PAYLOAD_MAX          14U
#define I2C_MEM_STORE_TRUSTED_ID_MAX              6U
#define I2C_MEM_STORE_TRUSTED_CODE_MAX            6U
/** @} */

/**
 * @brief Return codes for I2C memory store operations.
 */
typedef enum
{
    I2C_MEM_STORE_OK = 0,     /**< Success. */
    I2C_MEM_STORE_EINVAL,     /**< Invalid argument. */
    I2C_MEM_STORE_ESTATE,     /**< Invalid state or uninitialized context. */
    I2C_MEM_STORE_EHAL,       /**< HAL I2C operation failed. */
    I2C_MEM_STORE_ETIMEOUT,   /**< Timeout/error interpreted as timeout. */
    I2C_MEM_STORE_ECRC,       /**< CRC mismatch in persisted object. */
    I2C_MEM_STORE_EPROTO,     /**< Invalid record/protocol format in NVM. */
    I2C_MEM_STORE_ENOTFOUND,  /**< Requested object not found/empty. */
    I2C_MEM_STORE_EOVERFLOW,  /**< Buffer too small or payload too large. */
    I2C_MEM_STORE_EFULL       /**< Partition full or layout cannot fit. */
} i2c_mem_store_status_t;

/**
 * @brief One logged message record returned to caller.
 */
typedef struct
{
    uint32_t sequence;                                     /**< Monotonic record sequence. */
    uint32_t timestamp_ms;                                 /**< Timestamp from HAL_GetTick(). */
    int16_t rssi_dbm;                                      /**< RSSI attached to message. */
    uint8_t length;                                        /**< Message payload length. */
    uint8_t payload[I2C_MEM_STORE_LOG_PAYLOAD_MAX];        /**< Message payload bytes. */
} i2c_mem_store_log_record_t;

/**
 * @brief Trusted-device object stored in encrypted partition.
 */
typedef struct
{
    uint8_t id_len;                                        /**< Device identifier length. */
    uint8_t code_len;                                      /**< Device/service code length. */
    uint8_t id[I2C_MEM_STORE_TRUSTED_ID_MAX];              /**< Device identifier bytes. */
    uint8_t code[I2C_MEM_STORE_TRUSTED_CODE_MAX];          /**< Encrypted content payload before encryption. */
} i2c_mem_store_trusted_device_t;

/**
 * @brief Runtime configuration for external I2C memory.
 */
typedef struct
{
    I2C_HandleTypeDef *hi2c;          /**< I2C handle (use `&hi2c1`). */
    uint16_t i2c_addr_7bit;           /**< 7-bit memory address (default 0x50). */
    uint16_t mem_addr_size;           /**< I2C memory address size (`I2C_MEMADD_SIZE_8BIT/16BIT`). */
    uint32_t total_bytes;             /**< Total memory size in bytes. */
    uint16_t page_bytes;              /**< Write page size in bytes (EEPROM page). */
    uint16_t write_cycle_ms;          /**< EEPROM write cycle delay in ms. */
    uint32_t secret_area_bytes;       /**< Size of encrypted partition at end of memory. */
    uint8_t crypto_key[16];           /**< 128-bit XTEA key. */
} i2c_mem_store_cfg_t;

/**
 * @brief Driver context.
 */
typedef struct
{
    i2c_mem_store_cfg_t cfg;          /**< Active configuration. */
    uint16_t slot_count;              /**< Message log slot count. */
    uint16_t secret_slot_count;       /**< Secret partition slot count. */
    uint16_t write_slot;              /**< Next slot index to write in log partition. */
    uint16_t valid_count;             /**< Number of valid log entries. */
    uint32_t next_sequence;           /**< Next sequence number. */
    uint32_t secret_counter;          /**< Nonce/counter source for secret encryption. */
    bool initialized;                 /**< Context initialization flag. */
} i2c_mem_store_t;

/**
 * @brief Fill configuration with safe defaults for I2C1 memory.
 * @param cfg [out] Configuration object.
 * @param hi2c I2C handle (typically `&hi2c1`).
 */
void i2c_mem_store_default_cfg(i2c_mem_store_cfg_t *cfg, I2C_HandleTypeDef *hi2c);

/**
 * @brief Fill configuration specifically for ST M24C01-R (1 Kbit EEPROM).
 * @param cfg [out] Configuration object.
 * @param hi2c I2C handle (typically `&hi2c1`).
 */
void i2c_mem_store_default_cfg_m24c01r(i2c_mem_store_cfg_t *cfg, I2C_HandleTypeDef *hi2c);

/**
 * @brief Initialize library, load metadata and optionally format on first use.
 * @param ctx Context object.
 * @param cfg Runtime configuration.
 * @param format_if_needed If `true`, metadata is created when invalid/missing.
 * @return Status code.
 */
i2c_mem_store_status_t i2c_mem_store_init(i2c_mem_store_t *ctx,
                                          const i2c_mem_store_cfg_t *cfg,
                                          bool format_if_needed);

/**
 * @brief Format metadata and reset log/secret allocators.
 * @param ctx Context object.
 * @return Status code.
 */
i2c_mem_store_status_t i2c_mem_store_format(i2c_mem_store_t *ctx);

/**
 * @brief Append one received message to persistent ring log.
 * @param ctx Context object.
 * @param rssi_dbm RSSI associated with payload.
 * @param payload Message bytes.
 * @param payload_len Message length.
 * @return Status code.
 */
i2c_mem_store_status_t i2c_mem_store_append_message(i2c_mem_store_t *ctx,
                                                    int16_t rssi_dbm,
                                                    const uint8_t *payload,
                                                    uint8_t payload_len);

/**
 * @brief Read one message from log (0 = latest, 1 = previous, ...).
 * @param ctx Context object.
 * @param index_from_latest Relative index from latest entry.
 * @param out_record [out] Decoded record.
 * @return Status code.
 */
i2c_mem_store_status_t i2c_mem_store_read_message(i2c_mem_store_t *ctx,
                                                  uint16_t index_from_latest,
                                                  i2c_mem_store_log_record_t *out_record);

/**
 * @brief Get number of valid records currently available in ring log.
 * @param ctx Context object.
 * @param out_count [out] Record count.
 * @return Status code.
 */
i2c_mem_store_status_t i2c_mem_store_get_log_count(i2c_mem_store_t *ctx, uint16_t *out_count);

/**
 * @brief Store encrypted arbitrary payload in secret partition slot.
 * @param ctx Context object.
 * @param slot_id Slot index in secret partition.
 * @param data Plain payload.
 * @param data_len Payload length.
 * @return Status code.
 */
i2c_mem_store_status_t i2c_mem_store_secret_write(i2c_mem_store_t *ctx,
                                                  uint16_t slot_id,
                                                  const uint8_t *data,
                                                  uint8_t data_len);

/**
 * @brief Read and decrypt payload from secret partition slot.
 * @param ctx Context object.
 * @param slot_id Slot index in secret partition.
 * @param data_out Output buffer for decrypted payload.
 * @param data_capacity Output buffer capacity.
 * @param data_len [out] Returned payload length.
 * @return Status code.
 */
i2c_mem_store_status_t i2c_mem_store_secret_read(i2c_mem_store_t *ctx,
                                                 uint16_t slot_id,
                                                 uint8_t *data_out,
                                                 uint8_t data_capacity,
                                                 uint8_t *data_len);

/**
 * @brief Erase one secret slot (invalidate entry marker).
 * @param ctx Context object.
 * @param slot_id Slot index in secret partition.
 * @return Status code.
 */
i2c_mem_store_status_t i2c_mem_store_secret_erase(i2c_mem_store_t *ctx, uint16_t slot_id);

/**
 * @brief Store trusted-device descriptor in encrypted secret slot.
 * @param ctx Context object.
 * @param slot_id Slot index.
 * @param trusted Trusted-device object.
 * @return Status code.
 */
i2c_mem_store_status_t i2c_mem_store_trusted_device_write(i2c_mem_store_t *ctx,
                                                          uint16_t slot_id,
                                                          const i2c_mem_store_trusted_device_t *trusted);

/**
 * @brief Read trusted-device descriptor from encrypted secret slot.
 * @param ctx Context object.
 * @param slot_id Slot index.
 * @param trusted [out] Trusted-device object.
 * @return Status code.
 */
i2c_mem_store_status_t i2c_mem_store_trusted_device_read(i2c_mem_store_t *ctx,
                                                         uint16_t slot_id,
                                                         i2c_mem_store_trusted_device_t *trusted);

#ifdef __cplusplus
}
#endif

#endif /* APP_I2C_MEM_STORE_LIB_I2C_MEM_STORE_H_ */
