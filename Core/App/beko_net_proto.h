/**
 * @file beko_net_proto.h
 * @brief BEKO system message protocol (BEKO_NET_V1) helpers.
 *
 * `BEKO_NET_V1` jest prostą warstwą sieciową przenoszoną przez radio LoRa,
 * FSK lub OOK. Każda ramka niesie:
 * - `src_id`: stabilny identyfikator nadawcy,
 * - `dst_id`: odbiorcę unicast albo `BEKO_NET_BROADCAST_ID`,
 * - `msg_id`: monotonicznie rosnący numer wiadomości nadawcy,
 * - `ttl`: ograniczenie liczby przeskoków w mesh/relay,
 * - `flags`: znaczniki szyfrowania i auth,
 * - `payload`: dane aplikacyjne lub systemowe.
 *
 * Sieć działa w modelu store-and-forward:
 * - węzeł lokalny przyjmuje ramkę tylko po poprawnym dekodowaniu i CRC,
 * - ramka `USER` może być dalej przekazana tylko gdy nie jest dla mnie,
 *   nadal ma `ttl > 1` i nie została oznaczona jako replay/duplikat,
 * - ramki sterujące pairing/trust/ACK nie są forwardowane, bo są lokalną
 *   kontrolą sesji, a nie ruchem mesh.
 *
 * Ochrona przed replay działa per `src_id` przez przesuwne okno numerów
 * wiadomości. Cache trzyma najwyższy zaakceptowany `msg_id` i bitmapę
 * ostatnich numerów. Dzięki temu:
 * - natychmiastowe duplikaty są wykrywane,
 * - stare pakiety spoza okna są odrzucane jako replay,
 * - pakiety opóźnione, ale jeszcze mieszczące się w oknie, są akceptowane
 *   najwyżej raz.
 */

#ifndef APP_BEKO_NET_PROTO_H_
#define APP_BEKO_NET_PROTO_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BEKO_NET_MAGIC0                 ((uint8_t)'B')
#define BEKO_NET_MAGIC1                 ((uint8_t)'K')
#define BEKO_NET_VERSION                1U

#define BEKO_NET_DEFAULT_TTL            3U
#define BEKO_NET_BROADCAST_ID           0xFFFFFFFFUL
#define BEKO_NET_MAX_PAYLOAD            200U

#define BEKO_NET_FLAG_CODED             (1U << 0)
#define BEKO_NET_FLAG_AUTH              (1U << 1)

#define BEKO_NET_DEDUP_CAPACITY         32U
#define BEKO_NET_REPLAY_WINDOW_BITS     32U

typedef enum
{
    BEKO_NET_TYPE_USER = 0x01U,
    BEKO_NET_TYPE_JOIN_REQ = 0x10U,
    BEKO_NET_TYPE_JOIN_ACCEPT = 0x11U,
    BEKO_NET_TYPE_JOIN_REJECT = 0x12U,
    BEKO_NET_TYPE_TRUST_REMOVED = 0x13U,
    BEKO_NET_TYPE_ACK = 0x20U
} beko_net_type_t;

typedef struct
{
    uint8_t type;
    uint8_t flags;
    uint8_t ttl;
    uint32_t src_id;
    uint32_t dst_id;
    uint32_t msg_id;
    uint16_t payload_len;
    uint8_t payload[BEKO_NET_MAX_PAYLOAD];
} beko_net_frame_t;

typedef struct
{
    bool used;
    uint32_t src_id;
    uint32_t highest_msg_id;
    uint32_t recent_mask;
    uint32_t last_seen_ms;
} beko_net_dedup_entry_t;

typedef struct
{
    beko_net_dedup_entry_t entries[BEKO_NET_DEDUP_CAPACITY];
    uint32_t window_ms;
} beko_net_dedup_cache_t;

uint16_t beko_net_crc16(const uint8_t *data, uint16_t len);

uint32_t beko_net_local_node_id(void);

/**
 * @brief Koduje ramkę do formatu `BEKO_NET_V1`.
 *
 * Funkcja wymaga poprawnych pól sterujących. W szczególności `src_id`, `msg_id`
 * i `ttl` nie mogą być zerowe. Dzięki temu protokół odrzuca część oczywistych
 * ramek śmieciowych jeszcze przed wejściem do warstwy relay.
 */
bool beko_net_encode(const beko_net_frame_t *frame,
                     uint8_t *out,
                     uint16_t out_capacity,
                     uint16_t *out_len);

/**
 * @brief Dekoduje i waliduje ramkę `BEKO_NET_V1`.
 *
 * Oprócz pola długości i CRC funkcja odrzuca także ramki z zerowym `src_id`,
 * `msg_id` lub `ttl`, bo takie wartości nie są prawidłowe w sieci BEKO.
 */
bool beko_net_decode(const uint8_t *in,
                     uint16_t in_len,
                     beko_net_frame_t *frame_out);

void beko_net_xtea_ctr_crypt(uint8_t *data,
                             uint16_t len,
                             const uint8_t key[16],
                             uint32_t nonce);

bool beko_net_is_for_node(const beko_net_frame_t *frame, uint32_t self_node_id);

/**
 * @brief Sprawdza, czy ramka ma sens do dalszego relay.
 *
 * Forwardowane są wyłącznie ramki `USER`, które:
 * - nie pochodzą z lokalnego węzła,
 * - nie są zaadresowane bezpośrednio do lokalnego węzła,
 * - mają jeszcze zapas `ttl > 1`.
 */
bool beko_net_should_forward(const beko_net_frame_t *frame, uint32_t self_node_id);

void beko_net_dedup_init(beko_net_dedup_cache_t *cache, uint32_t window_ms);

/**
 * @brief Aktualizuje okno anty-replay dla danego `src_id`.
 * @return `true`, gdy ramka jest replayem/duplikatem i nie powinna być dalej
 *         przetwarzana ani forwardowana; `false`, gdy numer wiadomości został
 *         zaakceptowany do pierwszego użycia.
 */
bool beko_net_dedup_seen_or_add(beko_net_dedup_cache_t *cache,
                                uint32_t src_id,
                                uint32_t msg_id,
                                uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* APP_BEKO_NET_PROTO_H_ */
