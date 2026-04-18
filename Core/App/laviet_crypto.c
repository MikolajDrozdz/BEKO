/**
 * @file laviet_crypto.c
 * @brief Portable AES-CTR and HMAC-SHA256 backend for LAVIET_FRAME_V1.
 */

#include "laviet_crypto.h"

#include "cmsis_os2.h"
#include "stm32u5xx_hal.h"

#include <stdio.h>
#include <string.h>

#if defined(HAL_CRYP_MODULE_ENABLED)
extern CRYP_HandleTypeDef hcryp;
#endif
#if defined(HAL_HASH_MODULE_ENABLED)
extern HASH_HandleTypeDef hhash;
#endif

#define LAVIET_CRYPTO_TIMEOUT_MS  1000U

typedef union
{
    uint32_t words[4];
    uint8_t bytes[16];
} laviet_crypto_block_t;

static osMutexId_t s_crypto_mutex = NULL;
static bool s_crypto_hw_ready = false;

typedef struct
{
    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
} laviet_sha256_ctx_t;

static const uint32_t s_sha256_k[64] =
{
    0x428A2F98UL, 0x71374491UL, 0xB5C0FBCFUL, 0xE9B5DBA5UL,
    0x3956C25BUL, 0x59F111F1UL, 0x923F82A4UL, 0xAB1C5ED5UL,
    0xD807AA98UL, 0x12835B01UL, 0x243185BEUL, 0x550C7DC3UL,
    0x72BE5D74UL, 0x80DEB1FEUL, 0x9BDC06A7UL, 0xC19BF174UL,
    0xE49B69C1UL, 0xEFBE4786UL, 0x0FC19DC6UL, 0x240CA1CCUL,
    0x2DE92C6FUL, 0x4A7484AAUL, 0x5CB0A9DCUL, 0x76F988DAUL,
    0x983E5152UL, 0xA831C66DUL, 0xB00327C8UL, 0xBF597FC7UL,
    0xC6E00BF3UL, 0xD5A79147UL, 0x06CA6351UL, 0x14292967UL,
    0x27B70A85UL, 0x2E1B2138UL, 0x4D2C6DFCUL, 0x53380D13UL,
    0x650A7354UL, 0x766A0ABBUL, 0x81C2C92EUL, 0x92722C85UL,
    0xA2BFE8A1UL, 0xA81A664BUL, 0xC24B8B70UL, 0xC76C51A3UL,
    0xD192E819UL, 0xD6990624UL, 0xF40E3585UL, 0x106AA070UL,
    0x19A4C116UL, 0x1E376C08UL, 0x2748774CUL, 0x34B0BCB5UL,
    0x391C0CB3UL, 0x4ED8AA4AUL, 0x5B9CCA4FUL, 0x682E6FF3UL,
    0x748F82EEUL, 0x78A5636FUL, 0x84C87814UL, 0x8CC70208UL,
    0x90BEFFFAUL, 0xA4506CEBUL, 0xBEF9A3F7UL, 0xC67178F2UL
};

static const uint8_t s_aes_sbox[256] =
{
    0x63U, 0x7CU, 0x77U, 0x7BU, 0xF2U, 0x6BU, 0x6FU, 0xC5U,
    0x30U, 0x01U, 0x67U, 0x2BU, 0xFEU, 0xD7U, 0xABU, 0x76U,
    0xCAU, 0x82U, 0xC9U, 0x7DU, 0xFAU, 0x59U, 0x47U, 0xF0U,
    0xADU, 0xD4U, 0xA2U, 0xAFU, 0x9CU, 0xA4U, 0x72U, 0xC0U,
    0xB7U, 0xFDU, 0x93U, 0x26U, 0x36U, 0x3FU, 0xF7U, 0xCCU,
    0x34U, 0xA5U, 0xE5U, 0xF1U, 0x71U, 0xD8U, 0x31U, 0x15U,
    0x04U, 0xC7U, 0x23U, 0xC3U, 0x18U, 0x96U, 0x05U, 0x9AU,
    0x07U, 0x12U, 0x80U, 0xE2U, 0xEBU, 0x27U, 0xB2U, 0x75U,
    0x09U, 0x83U, 0x2CU, 0x1AU, 0x1BU, 0x6EU, 0x5AU, 0xA0U,
    0x52U, 0x3BU, 0xD6U, 0xB3U, 0x29U, 0xE3U, 0x2FU, 0x84U,
    0x53U, 0xD1U, 0x00U, 0xEDU, 0x20U, 0xFCU, 0xB1U, 0x5BU,
    0x6AU, 0xCBU, 0xBEU, 0x39U, 0x4AU, 0x4CU, 0x58U, 0xCFU,
    0xD0U, 0xEFU, 0xAAU, 0xFBU, 0x43U, 0x4DU, 0x33U, 0x85U,
    0x45U, 0xF9U, 0x02U, 0x7FU, 0x50U, 0x3CU, 0x9FU, 0xA8U,
    0x51U, 0xA3U, 0x40U, 0x8FU, 0x92U, 0x9DU, 0x38U, 0xF5U,
    0xBCU, 0xB6U, 0xDAU, 0x21U, 0x10U, 0xFFU, 0xF3U, 0xD2U,
    0xCDU, 0x0CU, 0x13U, 0xECU, 0x5FU, 0x97U, 0x44U, 0x17U,
    0xC4U, 0xA7U, 0x7EU, 0x3DU, 0x64U, 0x5DU, 0x19U, 0x73U,
    0x60U, 0x81U, 0x4FU, 0xDCU, 0x22U, 0x2AU, 0x90U, 0x88U,
    0x46U, 0xEEU, 0xB8U, 0x14U, 0xDEU, 0x5EU, 0x0BU, 0xDBU,
    0xE0U, 0x32U, 0x3AU, 0x0AU, 0x49U, 0x06U, 0x24U, 0x5CU,
    0xC2U, 0xD3U, 0xACU, 0x62U, 0x91U, 0x95U, 0xE4U, 0x79U,
    0xE7U, 0xC8U, 0x37U, 0x6DU, 0x8DU, 0xD5U, 0x4EU, 0xA9U,
    0x6CU, 0x56U, 0xF4U, 0xEAU, 0x65U, 0x7AU, 0xAEU, 0x08U,
    0xBAU, 0x78U, 0x25U, 0x2EU, 0x1CU, 0xA6U, 0xB4U, 0xC6U,
    0xE8U, 0xDDU, 0x74U, 0x1FU, 0x4BU, 0xBDU, 0x8BU, 0x8AU,
    0x70U, 0x3EU, 0xB5U, 0x66U, 0x48U, 0x03U, 0xF6U, 0x0EU,
    0x61U, 0x35U, 0x57U, 0xB9U, 0x86U, 0xC1U, 0x1DU, 0x9EU,
    0xE1U, 0xF8U, 0x98U, 0x11U, 0x69U, 0xD9U, 0x8EU, 0x94U,
    0x9BU, 0x1EU, 0x87U, 0xE9U, 0xCEU, 0x55U, 0x28U, 0xDFU,
    0x8CU, 0xA1U, 0x89U, 0x0DU, 0xBFU, 0xE6U, 0x42U, 0x68U,
    0x41U, 0x99U, 0x2DU, 0x0FU, 0xB0U, 0x54U, 0xBBU, 0x16U
};

static const uint8_t s_aes_rcon[11] =
{
    0x00U, 0x01U, 0x02U, 0x04U, 0x08U, 0x10U, 0x20U, 0x40U, 0x80U, 0x1BU, 0x36U
};

static uint32_t laviet_rotr32(uint32_t value, uint8_t bits)
{
    return (value >> bits) | (value << (32U - bits));
}

static void laviet_sha256_transform(laviet_sha256_ctx_t *ctx, const uint8_t data[64])
{
    uint32_t m[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    uint32_t i;

    for (i = 0U; i < 16U; i++)
    {
        m[i] = ((uint32_t)data[i * 4U] << 24) |
               ((uint32_t)data[(i * 4U) + 1U] << 16) |
               ((uint32_t)data[(i * 4U) + 2U] << 8) |
               (uint32_t)data[(i * 4U) + 3U];
    }
    for (i = 16U; i < 64U; i++)
    {
        uint32_t s0 = laviet_rotr32(m[i - 15U], 7U) ^
                      laviet_rotr32(m[i - 15U], 18U) ^
                      (m[i - 15U] >> 3);
        uint32_t s1 = laviet_rotr32(m[i - 2U], 17U) ^
                      laviet_rotr32(m[i - 2U], 19U) ^
                      (m[i - 2U] >> 10);
        m[i] = m[i - 16U] + s0 + m[i - 7U] + s1;
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0U; i < 64U; i++)
    {
        uint32_t s1 = laviet_rotr32(e, 6U) ^ laviet_rotr32(e, 11U) ^ laviet_rotr32(e, 25U);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + ch + s_sha256_k[i] + m[i];
        uint32_t s0 = laviet_rotr32(a, 2U) ^ laviet_rotr32(a, 13U) ^ laviet_rotr32(a, 22U);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void laviet_sha256_init(laviet_sha256_ctx_t *ctx)
{
    ctx->datalen = 0U;
    ctx->bitlen = 0ULL;
    ctx->state[0] = 0x6A09E667UL;
    ctx->state[1] = 0xBB67AE85UL;
    ctx->state[2] = 0x3C6EF372UL;
    ctx->state[3] = 0xA54FF53AUL;
    ctx->state[4] = 0x510E527FUL;
    ctx->state[5] = 0x9B05688CUL;
    ctx->state[6] = 0x1F83D9ABUL;
    ctx->state[7] = 0x5BE0CD19UL;
}

static void laviet_sha256_update(laviet_sha256_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    uint16_t i;

    if ((ctx == NULL) || ((data == NULL) && (len > 0U)))
    {
        return;
    }

    for (i = 0U; i < len; i++)
    {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        if (ctx->datalen == 64U)
        {
            laviet_sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512ULL;
            ctx->datalen = 0U;
        }
    }
}

static void laviet_sha256_final(laviet_sha256_ctx_t *ctx, uint8_t hash[LAVIET_SHA256_LEN])
{
    uint32_t i = ctx->datalen;
    uint8_t j;

    if (ctx->datalen < 56U)
    {
        ctx->data[i++] = 0x80U;
        while (i < 56U)
        {
            ctx->data[i++] = 0x00U;
        }
    }
    else
    {
        ctx->data[i++] = 0x80U;
        while (i < 64U)
        {
            ctx->data[i++] = 0x00U;
        }
        laviet_sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56U);
    }

    ctx->bitlen += ((uint64_t)ctx->datalen * 8ULL);
    ctx->data[63] = (uint8_t)ctx->bitlen;
    ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
    ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
    ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
    ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
    ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
    ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
    ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
    laviet_sha256_transform(ctx, ctx->data);

    for (i = 0U; i < 8U; i++)
    {
        for (j = 0U; j < 4U; j++)
        {
            hash[(i * 4U) + j] = (uint8_t)(ctx->state[i] >> (24U - (j * 8U)));
        }
    }
}

static uint8_t laviet_aes_xtime(uint8_t x)
{
    return (uint8_t)((x << 1) ^ (((x >> 7) & 1U) * 0x1BU));
}

static void laviet_aes_key_expansion(const uint8_t key[16], uint8_t round_key[176])
{
    uint8_t i;
    uint8_t j;
    uint8_t temp[4];

    memcpy(round_key, key, 16U);
    for (i = 4U; i < 44U; i++)
    {
        for (j = 0U; j < 4U; j++)
        {
            temp[j] = round_key[((i - 1U) * 4U) + j];
        }

        if ((i % 4U) == 0U)
        {
            uint8_t t = temp[0];
            temp[0] = (uint8_t)(s_aes_sbox[temp[1]] ^ s_aes_rcon[i / 4U]);
            temp[1] = s_aes_sbox[temp[2]];
            temp[2] = s_aes_sbox[temp[3]];
            temp[3] = s_aes_sbox[t];
        }

        for (j = 0U; j < 4U; j++)
        {
            round_key[(i * 4U) + j] = (uint8_t)(round_key[((i - 4U) * 4U) + j] ^ temp[j]);
        }
    }
}

static void laviet_aes_add_round_key(uint8_t state[16], const uint8_t *round_key)
{
    uint8_t i;

    for (i = 0U; i < 16U; i++)
    {
        state[i] ^= round_key[i];
    }
}

static void laviet_aes_sub_bytes(uint8_t state[16])
{
    uint8_t i;

    for (i = 0U; i < 16U; i++)
    {
        state[i] = s_aes_sbox[state[i]];
    }
}

static void laviet_aes_shift_rows(uint8_t state[16])
{
    uint8_t temp;

    temp = state[1];
    state[1] = state[5];
    state[5] = state[9];
    state[9] = state[13];
    state[13] = temp;

    temp = state[2];
    state[2] = state[10];
    state[10] = temp;
    temp = state[6];
    state[6] = state[14];
    state[14] = temp;

    temp = state[15];
    state[15] = state[11];
    state[11] = state[7];
    state[7] = state[3];
    state[3] = temp;
}

static void laviet_aes_mix_columns(uint8_t state[16])
{
    uint8_t i;

    for (i = 0U; i < 4U; i++)
    {
        uint8_t *col = &state[i * 4U];
        uint8_t t = (uint8_t)(col[0] ^ col[1] ^ col[2] ^ col[3]);
        uint8_t tmp = col[0];
        uint8_t tm;

        tm = (uint8_t)(col[0] ^ col[1]);
        tm = laviet_aes_xtime(tm);
        col[0] ^= (uint8_t)(tm ^ t);
        tm = (uint8_t)(col[1] ^ col[2]);
        tm = laviet_aes_xtime(tm);
        col[1] ^= (uint8_t)(tm ^ t);
        tm = (uint8_t)(col[2] ^ col[3]);
        tm = laviet_aes_xtime(tm);
        col[2] ^= (uint8_t)(tm ^ t);
        tm = (uint8_t)(col[3] ^ tmp);
        tm = laviet_aes_xtime(tm);
        col[3] ^= (uint8_t)(tm ^ t);
    }
}

static void laviet_aes_encrypt_block(const uint8_t in[16],
                                     uint8_t out[16],
                                     const uint8_t round_key[176])
{
    uint8_t state[16];
    uint8_t round;

    memcpy(state, in, 16U);
    laviet_aes_add_round_key(state, round_key);

    for (round = 1U; round < 10U; round++)
    {
        laviet_aes_sub_bytes(state);
        laviet_aes_shift_rows(state);
        laviet_aes_mix_columns(state);
        laviet_aes_add_round_key(state, &round_key[round * 16U]);
    }

    laviet_aes_sub_bytes(state);
    laviet_aes_shift_rows(state);
    laviet_aes_add_round_key(state, &round_key[160]);
    memcpy(out, state, 16U);
    laviet_secure_zero(state, sizeof(state));
}

static void laviet_make_counter_block(const laviet_frame_t *frame,
                                      uint16_t block_index,
                                      uint8_t out[16])
{
    out[0] = (uint8_t)'L';
    out[1] = (uint8_t)'V';
    out[2] = (uint8_t)'1';
    out[3] = 0U;
    out[4] = (uint8_t)(frame->src_id >> 8);
    out[5] = (uint8_t)frame->src_id;
    out[6] = (uint8_t)(frame->dst_id >> 8);
    out[7] = (uint8_t)frame->dst_id;
    out[8] = (uint8_t)(frame->msg_id >> 8);
    out[9] = (uint8_t)frame->msg_id;
    out[10] = (uint8_t)(frame->counter >> 24);
    out[11] = (uint8_t)(frame->counter >> 16);
    out[12] = (uint8_t)(frame->counter >> 8);
    out[13] = (uint8_t)frame->counter;
    out[14] = (uint8_t)(block_index >> 8);
    out[15] = (uint8_t)block_index;
}

static bool laviet_crypto_lock(void)
{
    if (s_crypto_mutex == NULL)
    {
        return false;
    }

    return (osMutexAcquire(s_crypto_mutex, LAVIET_CRYPTO_TIMEOUT_MS) == osOK);
}

static void laviet_crypto_unlock(void)
{
    if (s_crypto_mutex != NULL)
    {
        (void)osMutexRelease(s_crypto_mutex);
    }
}

static bool laviet_hmac_sha256_sw(const uint8_t *key,
                                  uint16_t key_len,
                                  const uint8_t *data,
                                  uint16_t data_len,
                                  uint8_t out[LAVIET_SHA256_LEN])
{
    uint8_t key_block[64];
    uint8_t inner_hash[LAVIET_SHA256_LEN];
    laviet_sha256_ctx_t ctx;
    uint8_t i;

    if ((key == NULL) || (out == NULL) || ((data == NULL) && (data_len > 0U)))
    {
        return false;
    }

    memset(key_block, 0, sizeof(key_block));
    if (key_len > 64U)
    {
        laviet_sha256_init(&ctx);
        laviet_sha256_update(&ctx, key, key_len);
        laviet_sha256_final(&ctx, key_block);
    }
    else if (key_len > 0U)
    {
        memcpy(key_block, key, key_len);
    }

    for (i = 0U; i < 64U; i++)
    {
        key_block[i] ^= 0x36U;
    }
    laviet_sha256_init(&ctx);
    laviet_sha256_update(&ctx, key_block, sizeof(key_block));
    laviet_sha256_update(&ctx, data, data_len);
    laviet_sha256_final(&ctx, inner_hash);

    for (i = 0U; i < 64U; i++)
    {
        key_block[i] ^= (uint8_t)(0x36U ^ 0x5CU);
    }
    laviet_sha256_init(&ctx);
    laviet_sha256_update(&ctx, key_block, sizeof(key_block));
    laviet_sha256_update(&ctx, inner_hash, sizeof(inner_hash));
    laviet_sha256_final(&ctx, out);

    laviet_secure_zero(&ctx, sizeof(ctx));
    laviet_secure_zero(inner_hash, sizeof(inner_hash));
    laviet_secure_zero(key_block, sizeof(key_block));
    return true;
}

static bool laviet_hmac_sha256_hw_locked(const uint8_t *key,
                                         uint16_t key_len,
                                         const uint8_t *data,
                                         uint16_t data_len,
                                         uint8_t out[LAVIET_SHA256_LEN])
{
#if defined(HAL_HASH_MODULE_ENABLED)
    bool ok = false;

    if ((key == NULL) ||
        (out == NULL) ||
        ((data == NULL) && (data_len > 0U)))
    {
        return false;
    }

    (void)HAL_HASH_DeInit(&hhash);
    hhash.Init.DataType = HASH_DATATYPE_8B;
    hhash.Init.KeySize = key_len;
    hhash.Init.pKey = (uint8_t *)key;
    if ((HAL_HASH_Init(&hhash) == HAL_OK) &&
        (HAL_HMACEx_SHA256_Start(&hhash,
                                 data,
                                 data_len,
                                 out,
                                 LAVIET_CRYPTO_TIMEOUT_MS) == HAL_OK))
    {
        ok = true;
    }

    return ok;
#else
    (void)key;
    (void)key_len;
    (void)data;
    (void)data_len;
    (void)out;
    return false;
#endif
}

bool laviet_frame_hmac_sha256(const laviet_frame_t *frame,
                              const uint8_t key[LAVIET_HMAC_KEY_LEN],
                              uint8_t out[LAVIET_MAC_TAG_LEN])
{
    uint8_t mac_input[LAVIET_FRAME_HEADER_LEN + LAVIET_MAX_PAYLOAD];
    uint8_t mac_input_len = 0U;
    bool ok;

    if ((frame == NULL) || (key == NULL) || (out == NULL))
    {
        return false;
    }
    if (!laviet_frame_build_mac_input(frame, mac_input, sizeof(mac_input), &mac_input_len))
    {
        return false;
    }

    ok = laviet_hmac_sha256(key, LAVIET_HMAC_KEY_LEN, mac_input, mac_input_len, out);
    laviet_secure_zero(mac_input, sizeof(mac_input));
    return ok;
}

static bool laviet_aes_ctr_crypt_sw(uint8_t *data,
                                    uint8_t len,
                                    const uint8_t key[LAVIET_AES_KEY_LEN],
                                    const laviet_frame_t *frame)
{
    uint8_t round_key[176];
    uint8_t counter_block[16];
    uint8_t stream[16];
    uint8_t offset = 0U;
    uint16_t block_index = 0U;

    if (((data == NULL) && (len > 0U)) || (key == NULL) || (frame == NULL))
    {
        return false;
    }

    laviet_aes_key_expansion(key, round_key);
    while (offset < len)
    {
        uint8_t i;

        laviet_make_counter_block(frame, block_index, counter_block);
        laviet_aes_encrypt_block(counter_block, stream, round_key);
        for (i = 0U; (i < 16U) && (offset < len); i++)
        {
            data[offset] ^= stream[i];
            offset++;
        }
        block_index++;
    }

    laviet_secure_zero(round_key, sizeof(round_key));
    laviet_secure_zero(counter_block, sizeof(counter_block));
    laviet_secure_zero(stream, sizeof(stream));
    return true;
}

static bool laviet_aes_ctr_crypt_hw_locked(uint8_t *data,
                                           uint8_t len,
                                           const uint8_t key[LAVIET_AES_KEY_LEN],
                                           const laviet_frame_t *frame)
{
#if defined(HAL_CRYP_MODULE_ENABLED)
    laviet_crypto_block_t key_block;
    laviet_crypto_block_t iv_block;
    laviet_crypto_block_t input_block;
    laviet_crypto_block_t output_block;
    bool ok = false;

    if (((data == NULL) && (len > 0U)) ||
        (key == NULL) ||
        (frame == NULL) ||
        (hcryp.Instance != AES))
    {
        return false;
    }
    if (len == 0U)
    {
        return true;
    }
    if (len > sizeof(input_block.bytes))
    {
        return false;
    }

    memset(&key_block, 0, sizeof(key_block));
    memset(&iv_block, 0, sizeof(iv_block));
    memset(&input_block, 0, sizeof(input_block));
    memset(&output_block, 0, sizeof(output_block));
    memcpy(key_block.bytes, key, LAVIET_AES_KEY_LEN);
    laviet_make_counter_block(frame, 0U, iv_block.bytes);
    memcpy(input_block.bytes, data, len);

    (void)HAL_CRYP_DeInit(&hcryp);
    hcryp.Instance = AES;
    hcryp.Init.DataType = CRYP_NO_SWAP;
    hcryp.Init.KeySize = CRYP_KEYSIZE_128B;
    hcryp.Init.pKey = key_block.words;
    hcryp.Init.pInitVect = iv_block.words;
    hcryp.Init.Algorithm = CRYP_AES_CTR;
    hcryp.Init.DataWidthUnit = CRYP_DATAWIDTHUNIT_BYTE;
    hcryp.Init.HeaderWidthUnit = CRYP_HEADERWIDTHUNIT_BYTE;
    hcryp.Init.KeyIVConfigSkip = CRYP_KEYIVCONFIG_ALWAYS;
    hcryp.Init.KeyMode = CRYP_KEYMODE_NORMAL;

    if ((HAL_CRYP_Init(&hcryp) == HAL_OK) &&
        (HAL_CRYP_Encrypt(&hcryp,
                          input_block.words,
                          len,
                          output_block.words,
                          LAVIET_CRYPTO_TIMEOUT_MS) == HAL_OK))
    {
        memcpy(data, output_block.bytes, len);
        ok = true;
    }

    laviet_secure_zero(&key_block, sizeof(key_block));
    laviet_secure_zero(&iv_block, sizeof(iv_block));
    laviet_secure_zero(&input_block, sizeof(input_block));
    laviet_secure_zero(&output_block, sizeof(output_block));
    return ok;
#else
    (void)data;
    (void)len;
    (void)key;
    (void)frame;
    return false;
#endif
}

static bool laviet_crypto_self_test_locked(void)
{
    static const uint8_t s_test_key[32] =
    {
        0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U,
        0x88U, 0x99U, 0xAAU, 0xBBU, 0xCCU, 0xDDU, 0xEEU, 0xFFU,
        0x10U, 0x21U, 0x32U, 0x43U, 0x54U, 0x65U, 0x76U, 0x87U,
        0x98U, 0xA9U, 0xBAU, 0xCBU, 0xDCU, 0xEDU, 0xFEU, 0x0FU
    };
    static const uint8_t s_test_data[19] =
    {
        (uint8_t)'L', (uint8_t)'A', (uint8_t)'V', (uint8_t)'I', (uint8_t)'E',
        (uint8_t)'T', (uint8_t)'_', (uint8_t)'H', (uint8_t)'W', (uint8_t)'_',
        (uint8_t)'T', (uint8_t)'E', (uint8_t)'S', (uint8_t)'T', 0x01U, 0x02U,
        0x03U, 0x04U, 0x05U
    };
    static const laviet_frame_t s_test_frame =
    {
        .ver_type = 0x11U,
        .flags = LAVIET_FLAG_ENCRYPTED,
        .src_id = 0x1234U,
        .dst_id = 0x0001U,
        .msg_id = 0x4567U,
        .counter = 0x89ABCDEFUL
    };
    uint8_t hmac_sw[LAVIET_SHA256_LEN];
    uint8_t hmac_hw[LAVIET_SHA256_LEN];
    uint8_t aes_sw_1[1] = { 0xA5U };
    uint8_t aes_hw_1[1] = { 0xA5U };
    uint8_t aes_sw_16[16];
    uint8_t aes_hw_16[16];
    uint8_t i;

    memset(hmac_sw, 0, sizeof(hmac_sw));
    memset(hmac_hw, 0, sizeof(hmac_hw));
    for (i = 0U; i < sizeof(aes_sw_16); i++)
    {
        aes_sw_16[i] = (uint8_t)(i * 7U);
        aes_hw_16[i] = aes_sw_16[i];
    }

    if (!laviet_hmac_sha256_sw(s_test_key, sizeof(s_test_key), s_test_data, sizeof(s_test_data), hmac_sw) ||
        !laviet_hmac_sha256_hw_locked(s_test_key, sizeof(s_test_key), s_test_data, sizeof(s_test_data), hmac_hw) ||
        (memcmp(hmac_sw, hmac_hw, sizeof(hmac_sw)) != 0))
    {
        return false;
    }
    if (!laviet_aes_ctr_crypt_sw(NULL, 0U, s_test_key, &s_test_frame) ||
        !laviet_aes_ctr_crypt_hw_locked(NULL, 0U, s_test_key, &s_test_frame))
    {
        return false;
    }
    if (!laviet_aes_ctr_crypt_sw(aes_sw_1, sizeof(aes_sw_1), s_test_key, &s_test_frame) ||
        !laviet_aes_ctr_crypt_hw_locked(aes_hw_1, sizeof(aes_hw_1), s_test_key, &s_test_frame) ||
        (memcmp(aes_sw_1, aes_hw_1, sizeof(aes_sw_1)) != 0))
    {
        return false;
    }
    if (!laviet_aes_ctr_crypt_sw(aes_sw_16, sizeof(aes_sw_16), s_test_key, &s_test_frame) ||
        !laviet_aes_ctr_crypt_hw_locked(aes_hw_16, sizeof(aes_hw_16), s_test_key, &s_test_frame) ||
        (memcmp(aes_sw_16, aes_hw_16, sizeof(aes_sw_16)) != 0))
    {
        return false;
    }
    if (!laviet_aes_ctr_crypt_hw_locked(aes_hw_16, sizeof(aes_hw_16), s_test_key, &s_test_frame) ||
        !laviet_aes_ctr_crypt_sw(aes_sw_16, sizeof(aes_sw_16), s_test_key, &s_test_frame) ||
        (memcmp(aes_sw_16, aes_hw_16, sizeof(aes_sw_16)) != 0))
    {
        return false;
    }

    return true;
}

bool laviet_crypto_init(void)
{
    if (s_crypto_mutex == NULL)
    {
        s_crypto_mutex = osMutexNew(NULL);
        if (s_crypto_mutex == NULL)
        {
            printf("CRYPTO: mutex create failed, using software backend\r\n");
            s_crypto_hw_ready = false;
            return true;
        }
    }

    s_crypto_hw_ready = false;

#if defined(HAL_CRYP_MODULE_ENABLED) && defined(HAL_HASH_MODULE_ENABLED)
    if ((hcryp.Instance != AES) ||
        (HAL_CRYP_GetState(&hcryp) != HAL_CRYP_STATE_READY) ||
        (HAL_HASH_GetState(&hhash) != HAL_HASH_STATE_READY))
    {
        printf("CRYPTO: hardware backend not ready, using software fallback\r\n");
        return true;
    }

    if (!laviet_crypto_lock())
    {
        printf("CRYPTO: hardware lock unavailable, using software fallback\r\n");
        return true;
    }

    s_crypto_hw_ready = laviet_crypto_self_test_locked();
    laviet_crypto_unlock();

    if (s_crypto_hw_ready)
    {
        printf("CRYPTO: hardware backend ready\r\n");
    }
    else
    {
        printf("CRYPTO: hardware self-test failed, using software fallback\r\n");
    }

    return true;
#else
    printf("CRYPTO: hardware backend disabled, using software backend\r\n");
    return true;
#endif
}

bool laviet_hmac_sha256(const uint8_t *key,
                        uint16_t key_len,
                        const uint8_t *data,
                        uint16_t data_len,
                        uint8_t out[LAVIET_SHA256_LEN])
{
    if ((key == NULL) || (out == NULL) || ((data == NULL) && (data_len > 0U)))
    {
        return false;
    }

    if (s_crypto_hw_ready && laviet_crypto_lock())
    {
        bool ok = laviet_hmac_sha256_hw_locked(key, key_len, data, data_len, out);

        laviet_crypto_unlock();
        if (ok)
        {
            return true;
        }
    }

    return laviet_hmac_sha256_sw(key, key_len, data, data_len, out);
}

bool laviet_aes_ctr_crypt(uint8_t *data,
                          uint8_t len,
                          const uint8_t key[LAVIET_AES_KEY_LEN],
                          const laviet_frame_t *frame)
{
    uint8_t temp[16];

    if (((data == NULL) && (len > 0U)) || (key == NULL) || (frame == NULL))
    {
        return false;
    }

    if (s_crypto_hw_ready && (len <= sizeof(temp)) && laviet_crypto_lock())
    {
        bool ok;

        memset(temp, 0, sizeof(temp));
        if (len > 0U)
        {
            memcpy(temp, data, len);
        }
        ok = laviet_aes_ctr_crypt_hw_locked(temp, len, key, frame);
        laviet_crypto_unlock();
        if (ok)
        {
            if (len > 0U)
            {
                memcpy(data, temp, len);
            }
            laviet_secure_zero(temp, sizeof(temp));
            return true;
        }
        laviet_secure_zero(temp, sizeof(temp));
    }

    return laviet_aes_ctr_crypt_sw(data, len, key, frame);
}

bool laviet_mac_equal(const uint8_t a[LAVIET_MAC_TAG_LEN],
                      const uint8_t b[LAVIET_MAC_TAG_LEN])
{
    uint8_t diff = 0U;
    uint8_t i;

    if ((a == NULL) || (b == NULL))
    {
        return false;
    }

    for (i = 0U; i < LAVIET_MAC_TAG_LEN; i++)
    {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }

    return diff == 0U;
}

void laviet_secure_zero(void *ptr, size_t len)
{
    volatile uint8_t *p = (volatile uint8_t *)ptr;

    if (ptr == NULL)
    {
        return;
    }
    while (len > 0U)
    {
        *p = 0U;
        p++;
        len--;
    }
}
