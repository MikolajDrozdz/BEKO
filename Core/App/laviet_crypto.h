/**
 * @file laviet_crypto.h
 * @brief Crypto helpers for LAVIET_FRAME_V1.
 */

#ifndef APP_LAVIET_CRYPTO_H_
#define APP_LAVIET_CRYPTO_H_

#include "laviet_frame.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief AES-128 key length used by LAVIET frame encryption. */
#define LAVIET_AES_KEY_LEN      16U
/** @brief HMAC-SHA256 key length used by LAVIET frame authentication. */
#define LAVIET_HMAC_KEY_LEN     32U
/** @brief SHA-256 digest length in bytes. */
#define LAVIET_SHA256_LEN       32U

/**
 * @brief Initialize crypto backends and run the optional hardware self-test.
 * @return `true` when the software fallback is usable.
 */
bool laviet_crypto_init(void);

/**
 * @brief Calculate HMAC-SHA256 over an arbitrary buffer.
 * @param key HMAC key bytes.
 * @param key_len HMAC key length.
 * @param data Data buffer.
 * @param data_len Data length.
 * @param out [out] 32-byte digest output.
 * @return `true` when the digest was generated.
 */
bool laviet_hmac_sha256(const uint8_t *key,
                        uint16_t key_len,
                        const uint8_t *data,
                        uint16_t data_len,
                        uint8_t out[LAVIET_SHA256_LEN]);

/**
 * @brief Calculate the LAVIET frame MAC over the serialized MAC input.
 * @param frame Frame to authenticate.
 * @param key HMAC key.
 * @param out [out] MAC tag buffer.
 * @return `true` when the MAC tag was generated.
 */
bool laviet_frame_hmac_sha256(const laviet_frame_t *frame,
                              const uint8_t key[LAVIET_HMAC_KEY_LEN],
                              uint8_t out[LAVIET_MAC_TAG_LEN]);

/**
 * @brief Encrypt or decrypt a LAVIET payload in-place with AES-CTR.
 * @param data Payload buffer.
 * @param len Payload length.
 * @param key AES-128 key.
 * @param frame Frame fields used to build the CTR nonce.
 * @return `true` when the operation succeeded.
 */
bool laviet_aes_ctr_crypt(uint8_t *data,
                          uint8_t len,
                          const uint8_t key[LAVIET_AES_KEY_LEN],
                          const laviet_frame_t *frame);

/**
 * @brief Compare two MAC tags without data-dependent early exit.
 * @param a First MAC tag.
 * @param b Second MAC tag.
 * @return `true` when tags are identical.
 */
bool laviet_mac_equal(const uint8_t a[LAVIET_MAC_TAG_LEN],
                      const uint8_t b[LAVIET_MAC_TAG_LEN]);

/**
 * @brief Clear sensitive memory through a volatile byte pointer.
 * @param ptr Buffer to wipe.
 * @param len Number of bytes to clear.
 */
void laviet_secure_zero(void *ptr, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* APP_LAVIET_CRYPTO_H_ */
