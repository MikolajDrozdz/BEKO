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

#define LAVIET_AES_KEY_LEN      16U
#define LAVIET_HMAC_KEY_LEN     32U
#define LAVIET_SHA256_LEN       32U

bool laviet_crypto_init(void);
bool laviet_hmac_sha256(const uint8_t *key,
                        uint16_t key_len,
                        const uint8_t *data,
                        uint16_t data_len,
                        uint8_t out[LAVIET_SHA256_LEN]);
bool laviet_frame_hmac_sha256(const laviet_frame_t *frame,
                              const uint8_t key[LAVIET_HMAC_KEY_LEN],
                              uint8_t out[LAVIET_MAC_TAG_LEN]);
bool laviet_aes_ctr_crypt(uint8_t *data,
                          uint8_t len,
                          const uint8_t key[LAVIET_AES_KEY_LEN],
                          const laviet_frame_t *frame);
bool laviet_mac_equal(const uint8_t a[LAVIET_MAC_TAG_LEN],
                      const uint8_t b[LAVIET_MAC_TAG_LEN]);
void laviet_secure_zero(void *ptr, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* APP_LAVIET_CRYPTO_H_ */
