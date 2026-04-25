#include "menu_main.h"

#include "laviet_frame.h"
#include "bmp280_main.h"
#include "button_main.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "lcd_main.h"
#include "led_array_lib/led_array_lib.h"
#include "radio_main.h"
#include "radio_lib/radio_lib.h"
#include "security_main.h"
#include "task.h"
#include "tof_main.h"

#include <string.h>

#define MENU_TASK_STACK_SIZE                8192U
#define MENU_TASK_STACK_WORDS               (MENU_TASK_STACK_SIZE / sizeof(StackType_t))
#define MENU_NOTIFY_QUEUE_DEPTH             64U
#define MENU_INACTIVITY_TIMEOUT_MS          60000UL
#define MENU_DISPLAY_ROWS                   4U
#define MENU_ITEMS_VISIBLE                  3U
#define MENU_LINE_CHARS                     20U
#define MENU_LINE_BUF_SIZE                  (MENU_LINE_CHARS + 1U)
#define MENU_SETTINGS_PIN_LEN               4U
#define MENU_SETTINGS_PIN_UNLOCK_MS         60000UL
#define MENU_PAIRING_WINDOW_MS              300000UL
#define MENU_TRUSTED_DEVICE_SLOTS           16U
#define MENU_TRUSTED_NODE_SLOTS             15U
#define MENU_DEVICE_SLOT_INVALID            0xFFU

typedef enum
{
    MENU_PAGE_NONE = 0,
    MENU_PAGE_PAGER,
    MENU_PAGE_RADIO_SETTINGS,
    MENU_PAGE_SEND_OPTIONS,
    MENU_PAGE_SEND_DIRECT_LIST,
    MENU_PAGE_SEND_TARGET_LIST,
    MENU_PAGE_MSG_GROUPS,
    MENU_PAGE_GROUP_ALERT,
    MENU_PAGE_GROUP_STATUS,
    MENU_PAGE_GROUP_SERVICE,
    MENU_PAGE_GROUP_QUICK,
    MENU_PAGE_MAIN,
    MENU_PAGE_DEVICES,
    MENU_PAGE_DEVICE_DELETE_LIST,
    MENU_PAGE_DEVICE_DELETE_ACTION,
    MENU_PAGE_PIN_SETTINGS,
    MENU_PAGE_SECURITY,
    MENU_PAGE_SECURITY_FH,
    MENU_PAGE_SECURITY_CODING,
    MENU_PAGE_SECURITY_NOTIFY,
    MENU_PAGE_SECURITY_AUTOPING,
    MENU_PAGE_HARDWARE,
    MENU_PAGE_HARDWARE_LED,
    MENU_PAGE_MODULATION,
    MENU_PAGE_MOD_LORA,
    MENU_PAGE_MOD_LORA_FREQ,
    MENU_PAGE_MOD_LORA_BW,
    MENU_PAGE_MOD_LORA_SF,
    MENU_PAGE_MOD_LORA_CR,
    MENU_PAGE_MOD_LORA_POWER,
    MENU_PAGE_MOD_LORA_CRC,
    MENU_PAGE_MOD_LORA_PREAMBLE,
    MENU_PAGE_MOD_LORA_HEADER,
    MENU_PAGE_MOD_LORA_IQ,
    MENU_PAGE_MOD_LORA_SYNC,
    MENU_PAGE_MOD_FSK,
    MENU_PAGE_MOD_FSK_SHAPING,
    MENU_PAGE_MOD_FSK_FREQ,
    MENU_PAGE_MOD_FSK_BITRATE,
    MENU_PAGE_MOD_FSK_BW,
    MENU_PAGE_MOD_FSK_FILTER,
    MENU_PAGE_MOD_FSK_POWER,
    MENU_PAGE_MOD_FSK_PREAMBLE,
    MENU_PAGE_MOD_FSK_SYNC_LEN,
    MENU_PAGE_MOD_FSK_SYNC_WORD,
    MENU_PAGE_MOD_FSK_ADDR,
    MENU_PAGE_MOD_FSK_CRC,
    MENU_PAGE_MOD_FSK_WHITEN,
    MENU_PAGE_MOD_OOK,
    MENU_PAGE_MOD_OOK_FREQ,
    MENU_PAGE_MOD_OOK_BITRATE,
    MENU_PAGE_MOD_OOK_POWER,
    MENU_PAGE_MOD_OOK_BW,
    MENU_PAGE_MOD_OOK_PREAMBLE,
    MENU_PAGE_MOD_OOK_SYNC_LEN,
    MENU_PAGE_MOD_OOK_SYNC_WORD,
    MENU_PAGE_MOD_OOK_THRESH_TYPE,
    MENU_PAGE_MOD_OOK_THRESH_VALUE,
    MENU_PAGE_INFO
} menu_page_id_t;

typedef enum
{
    MENU_ACTION_NONE = 0,
    MENU_ACTION_BACK,
    MENU_ACTION_EXIT_TO_MONITOR,
    MENU_ACTION_SEND_DEFAULT,
    MENU_ACTION_SEND_DIRECT_DEFAULT,
    MENU_ACTION_SEND_ALERT_FIRE,
    MENU_ACTION_SEND_ALERT_INTR,
    MENU_ACTION_SEND_ALERT_LOWBATT,
    MENU_ACTION_SEND_STATUS_OK,
    MENU_ACTION_SEND_STATUS_BUSY,
    MENU_ACTION_SEND_STATUS_IDLE,
    MENU_ACTION_SEND_SERVICE_PING,
    MENU_ACTION_SEND_SERVICE_RESET,
    MENU_ACTION_SEND_SERVICE_SYNC,
    MENU_ACTION_SEND_ASK_DONE,
    MENU_ACTION_SEND_ACT_COME_OVER,
    MENU_ACTION_SEND_ACT_STOP,
    MENU_ACTION_SEND_ASK_READY,
    MENU_ACTION_DEVICE_ADD,
    MENU_ACTION_DEVICE_ADD_NETWORK,
    MENU_ACTION_DEVICE_DELETE,
    MENU_ACTION_DEVICE_DELETE_CONFIRM,
    MENU_ACTION_DEVICE_INFO,
    MENU_ACTION_PIN_TOGGLE,
    MENU_ACTION_PIN_SET_USER,
    MENU_ACTION_PIN_SET_ADMIN,
    MENU_ACTION_SEC_TOGGLE_FH,
    MENU_ACTION_SEC_FH_ENABLE,
    MENU_ACTION_SEC_FH_DISABLE,
    MENU_ACTION_SEC_FH_PERIOD_1000,
    MENU_ACTION_SEC_FH_PERIOD_2000,
    MENU_ACTION_SEC_FH_PERIOD_5000,
    MENU_ACTION_SEC_FH_PERIOD_10000,
    MENU_ACTION_SEC_TPM_INFO,
    MENU_ACTION_SEC_ROTATE_KEYS,
    MENU_ACTION_SEC_CODING_ON,
    MENU_ACTION_SEC_CODING_OFF,
    MENU_ACTION_SEC_NOTIFY_POPUP,
    MENU_ACTION_SEC_NOTIFY_BADGE,
    MENU_ACTION_SEC_AUTOPING_ON,
    MENU_ACTION_SEC_AUTOPING_OFF,
    MENU_ACTION_SEC_TOGGLE_CODING,
    MENU_ACTION_SEC_TOGGLE_NOTIFY_MODE,
    MENU_ACTION_SEC_TOGGLE_AUTOPING,
    MENU_ACTION_HW_MEASURE_DIST,
    MENU_ACTION_HW_MEASURE_TEMP,
    MENU_ACTION_HW_MEASURE_PRESS,
    MENU_ACTION_HW_LED_RAINBOW,
    MENU_ACTION_HW_LED_BREATH,
    MENU_ACTION_HW_LED_OFF,
    MENU_ACTION_HW_LED_MODE,
    MENU_ACTION_HW_RADIO_RESET,
    MENU_ACTION_MOD_LORA_ENABLE,
    MENU_ACTION_MOD_LORA_STD,
    MENU_ACTION_MOD_LORA_RANGE,
    MENU_ACTION_MOD_LORA_FAST,
    MENU_ACTION_MOD_LORA_FREQ_8680,
    MENU_ACTION_MOD_LORA_FREQ_8681,
    MENU_ACTION_MOD_LORA_FREQ_8683,
    MENU_ACTION_MOD_LORA_FREQ_8685,
    MENU_ACTION_MOD_LORA_FREQ_8688,
    MENU_ACTION_MOD_LORA_FREQ_86905,
    MENU_ACTION_MOD_LORA_FREQ_869525,
    MENU_ACTION_MOD_LORA_BW_7_8,
    MENU_ACTION_MOD_LORA_BW_10_4,
    MENU_ACTION_MOD_LORA_BW_15_6,
    MENU_ACTION_MOD_LORA_BW_20_8,
    MENU_ACTION_MOD_LORA_BW_31_25,
    MENU_ACTION_MOD_LORA_BW_41_7,
    MENU_ACTION_MOD_LORA_BW_62_5,
    MENU_ACTION_MOD_LORA_BW_125,
    MENU_ACTION_MOD_LORA_BW_250,
    MENU_ACTION_MOD_LORA_BW_500,
    MENU_ACTION_MOD_LORA_SF_6,
    MENU_ACTION_MOD_LORA_SF_7,
    MENU_ACTION_MOD_LORA_SF_8,
    MENU_ACTION_MOD_LORA_SF_9,
    MENU_ACTION_MOD_LORA_SF_10,
    MENU_ACTION_MOD_LORA_SF_11,
    MENU_ACTION_MOD_LORA_SF_12,
    MENU_ACTION_MOD_LORA_CR_45,
    MENU_ACTION_MOD_LORA_CR_46,
    MENU_ACTION_MOD_LORA_CR_47,
    MENU_ACTION_MOD_LORA_CR_48,
    MENU_ACTION_MOD_LORA_PWR_2,
    MENU_ACTION_MOD_LORA_PWR_5,
    MENU_ACTION_MOD_LORA_PWR_8,
    MENU_ACTION_MOD_LORA_PWR_11,
    MENU_ACTION_MOD_LORA_PWR_14,
    MENU_ACTION_MOD_LORA_PWR_17,
    MENU_ACTION_MOD_LORA_PWR_20,
    MENU_ACTION_MOD_LORA_CRC_OFF,
    MENU_ACTION_MOD_LORA_CRC_ON,
    MENU_ACTION_MOD_LORA_PREAMBLE_6,
    MENU_ACTION_MOD_LORA_PREAMBLE_8,
    MENU_ACTION_MOD_LORA_PREAMBLE_12,
    MENU_ACTION_MOD_LORA_PREAMBLE_16,
    MENU_ACTION_MOD_LORA_PREAMBLE_24,
    MENU_ACTION_MOD_LORA_PREAMBLE_32,
    MENU_ACTION_MOD_LORA_HEADER_EXPLICIT,
    MENU_ACTION_MOD_LORA_HEADER_IMPLICIT,
    MENU_ACTION_MOD_LORA_IQ_NORMAL,
    MENU_ACTION_MOD_LORA_IQ_INVERT,
    MENU_ACTION_MOD_LORA_SYNC_12,
    MENU_ACTION_MOD_LORA_SYNC_34,
    MENU_ACTION_MOD_LORA_SYNC_56,
    MENU_ACTION_MOD_LORA_SYNC_A5,
    MENU_ACTION_MOD_LORA_RESET,
    MENU_ACTION_MOD_FSK_ENABLE,
    MENU_ACTION_MOD_FSK_SHAPING_FSK,
    MENU_ACTION_MOD_FSK_SHAPING_GFSK,
    MENU_ACTION_MOD_FSK_SHAPING_MSK,
    MENU_ACTION_MOD_FSK_SHAPING_GMSK,
    MENU_ACTION_MOD_FSK_FREQ_8680,
    MENU_ACTION_MOD_FSK_FREQ_8681,
    MENU_ACTION_MOD_FSK_FREQ_8683,
    MENU_ACTION_MOD_FSK_FREQ_8685,
    MENU_ACTION_MOD_FSK_FREQ_8688,
    MENU_ACTION_MOD_FSK_FREQ_86905,
    MENU_ACTION_MOD_FSK_FREQ_869525,
    MENU_ACTION_MOD_FSK_BITRATE_1200,
    MENU_ACTION_MOD_FSK_BITRATE_2400,
    MENU_ACTION_MOD_FSK_BITRATE_4800,
    MENU_ACTION_MOD_FSK_BITRATE_9600,
    MENU_ACTION_MOD_FSK_BITRATE_19200,
    MENU_ACTION_MOD_FSK_BITRATE_38400,
    MENU_ACTION_MOD_FSK_BITRATE_50000,
    MENU_ACTION_MOD_FSK_BITRATE_100000,
    MENU_ACTION_MOD_FSK_BW_31_25,
    MENU_ACTION_MOD_FSK_BW_62_5,
    MENU_ACTION_MOD_FSK_BW_125,
    MENU_ACTION_MOD_FSK_BW_250,
    MENU_ACTION_MOD_FSK_BW_500,
    MENU_ACTION_MOD_FSK_FILTER_NONE,
    MENU_ACTION_MOD_FSK_FILTER_BT10,
    MENU_ACTION_MOD_FSK_FILTER_BT07,
    MENU_ACTION_MOD_FSK_FILTER_BT05,
    MENU_ACTION_MOD_FSK_FILTER_BT03,
    MENU_ACTION_MOD_FSK_PWR_2,
    MENU_ACTION_MOD_FSK_PWR_5,
    MENU_ACTION_MOD_FSK_PWR_8,
    MENU_ACTION_MOD_FSK_PWR_11,
    MENU_ACTION_MOD_FSK_PWR_14,
    MENU_ACTION_MOD_FSK_PWR_17,
    MENU_ACTION_MOD_FSK_PWR_20,
    MENU_ACTION_MOD_FSK_PREAMBLE_4,
    MENU_ACTION_MOD_FSK_PREAMBLE_8,
    MENU_ACTION_MOD_FSK_PREAMBLE_12,
    MENU_ACTION_MOD_FSK_PREAMBLE_16,
    MENU_ACTION_MOD_FSK_PREAMBLE_24,
    MENU_ACTION_MOD_FSK_PREAMBLE_32,
    MENU_ACTION_MOD_FSK_SYNC_LEN_0,
    MENU_ACTION_MOD_FSK_SYNC_LEN_1,
    MENU_ACTION_MOD_FSK_SYNC_LEN_2,
    MENU_ACTION_MOD_FSK_SYNC_LEN_3,
    MENU_ACTION_MOD_FSK_SYNC_LEN_4,
    MENU_ACTION_MOD_FSK_SYNC_WORD_55AA,
    MENU_ACTION_MOD_FSK_SYNC_WORD_2DD4,
    MENU_ACTION_MOD_FSK_SYNC_WORD_A55A,
    MENU_ACTION_MOD_FSK_SYNC_WORD_C194C1,
    MENU_ACTION_MOD_FSK_SYNC_WORD_1ACFFC1D,
    MENU_ACTION_MOD_FSK_ADDR_NONE,
    MENU_ACTION_MOD_FSK_ADDR_NODE,
    MENU_ACTION_MOD_FSK_ADDR_NODE_BC,
    MENU_ACTION_MOD_FSK_CRC_OFF,
    MENU_ACTION_MOD_FSK_CRC_IBM,
    MENU_ACTION_MOD_FSK_CRC_CCITT,
    MENU_ACTION_MOD_FSK_WHITEN_OFF,
    MENU_ACTION_MOD_FSK_WHITEN_ON,
    MENU_ACTION_MOD_FSK_RESET,
    MENU_ACTION_MOD_OOK_ENABLE,
    MENU_ACTION_MOD_OOK_FREQ_8680,
    MENU_ACTION_MOD_OOK_FREQ_8681,
    MENU_ACTION_MOD_OOK_FREQ_8683,
    MENU_ACTION_MOD_OOK_FREQ_8685,
    MENU_ACTION_MOD_OOK_FREQ_8688,
    MENU_ACTION_MOD_OOK_FREQ_86905,
    MENU_ACTION_MOD_OOK_FREQ_869525,
    MENU_ACTION_MOD_OOK_BITRATE_1200,
    MENU_ACTION_MOD_OOK_BITRATE_2400,
    MENU_ACTION_MOD_OOK_BITRATE_4800,
    MENU_ACTION_MOD_OOK_BITRATE_9600,
    MENU_ACTION_MOD_OOK_BITRATE_19200,
    MENU_ACTION_MOD_OOK_BITRATE_38400,
    MENU_ACTION_MOD_OOK_BITRATE_50000,
    MENU_ACTION_MOD_OOK_BITRATE_100000,
    MENU_ACTION_MOD_OOK_PWR_2,
    MENU_ACTION_MOD_OOK_PWR_5,
    MENU_ACTION_MOD_OOK_PWR_8,
    MENU_ACTION_MOD_OOK_PWR_11,
    MENU_ACTION_MOD_OOK_PWR_14,
    MENU_ACTION_MOD_OOK_PWR_17,
    MENU_ACTION_MOD_OOK_PWR_20,
    MENU_ACTION_MOD_OOK_BW_31_25,
    MENU_ACTION_MOD_OOK_BW_62_5,
    MENU_ACTION_MOD_OOK_BW_125,
    MENU_ACTION_MOD_OOK_BW_250,
    MENU_ACTION_MOD_OOK_BW_500,
    MENU_ACTION_MOD_OOK_PREAMBLE_4,
    MENU_ACTION_MOD_OOK_PREAMBLE_8,
    MENU_ACTION_MOD_OOK_PREAMBLE_12,
    MENU_ACTION_MOD_OOK_PREAMBLE_16,
    MENU_ACTION_MOD_OOK_PREAMBLE_24,
    MENU_ACTION_MOD_OOK_PREAMBLE_32,
    MENU_ACTION_MOD_OOK_SYNC_LEN_0,
    MENU_ACTION_MOD_OOK_SYNC_LEN_1,
    MENU_ACTION_MOD_OOK_SYNC_LEN_2,
    MENU_ACTION_MOD_OOK_SYNC_LEN_3,
    MENU_ACTION_MOD_OOK_SYNC_LEN_4,
    MENU_ACTION_MOD_OOK_SYNC_WORD_55AA,
    MENU_ACTION_MOD_OOK_SYNC_WORD_2DD4,
    MENU_ACTION_MOD_OOK_SYNC_WORD_A55A,
    MENU_ACTION_MOD_OOK_SYNC_WORD_C194C1,
    MENU_ACTION_MOD_OOK_SYNC_WORD_1ACFFC1D,
    MENU_ACTION_MOD_OOK_THRESH_FIXED,
    MENU_ACTION_MOD_OOK_THRESH_PEAK,
    MENU_ACTION_MOD_OOK_THRESH_AVG,
    MENU_ACTION_MOD_OOK_THRESH_4,
    MENU_ACTION_MOD_OOK_THRESH_8,
    MENU_ACTION_MOD_OOK_THRESH_12,
    MENU_ACTION_MOD_OOK_THRESH_16,
    MENU_ACTION_MOD_OOK_THRESH_24,
    MENU_ACTION_MOD_OOK_THRESH_32,
    MENU_ACTION_MOD_OOK_THRESH_48,
    MENU_ACTION_MOD_OOK_THRESH_64,
    MENU_ACTION_MOD_OOK_RESET,
    MENU_ACTION_MOD_FSK,
    MENU_ACTION_MOD_OOK,
    MENU_ACTION_INFO_SHOW
} menu_action_t;

typedef struct
{
    const char *label;
    menu_page_id_t child_page;
    menu_action_t action;
} menu_item_t;

typedef struct
{
    const char *title;
    menu_page_id_t parent;
    const menu_item_t *items;
    uint8_t item_count;
} menu_page_t;

typedef enum
{
    MENU_MODAL_NONE = 0,
    MENU_MODAL_INFO,
    MENU_MODAL_PAIR_REQUEST,
    MENU_MODAL_PAIR_SETUP,
    MENU_MODAL_SEND_CONFIRM,
    MENU_MODAL_QUICK_REPLY,
    MENU_MODAL_PIN
} menu_modal_t;

typedef enum
{
    MENU_AUTH_NONE = 0,
    MENU_AUTH_USER,
    MENU_AUTH_ADMIN
} menu_auth_level_t;

typedef struct
{
    menu_page_id_t current_page;
    uint8_t selected_idx;
    uint8_t led_mode;
    uint8_t selected_device_slot;
    uint32_t pending_target_node_id;
    menu_action_t send_target_action;
    menu_page_id_t transient_parent_page;
    bool pairing_network_mode;
    uint32_t quick_reply_src_id;
    uint32_t quick_reply_deadline_ms;
    uint32_t quick_reply_last_seconds;
    char quick_reply_text[MENU_LINE_BUF_SIZE];
    bool tx_ack_waiting;
    char tx_sent_text[MENU_LINE_BUF_SIZE];
    menu_page_id_t pending_auth_page;
    menu_auth_level_t pending_auth_level;
    uint8_t pin_digits[MENU_SETTINGS_PIN_LEN];
    uint8_t pin_index;
    uint32_t user_unlock_until_ms;
    uint32_t admin_unlock_until_ms;
    bool popup_enabled;
    uint32_t last_input_ms;
    menu_modal_t modal;
    menu_action_t pending_action;
} menu_state_t;

typedef struct
{
    menu_action_t action;
    bool set_modulation;
    radio_main_modulation_t modulation;
    bool set_option;
    radio_main_option_t option;
    uint32_t value;
    bool persist_lora_preset;
    const char *ok_text;
    const char *err_text;
} menu_radio_action_binding_t;

static osThreadId_t s_menu_task = NULL;
static osMessageQueueId_t s_menu_notify_queue = NULL;
static StaticTask_t s_menu_task_cb;
static StackType_t s_menu_task_stack[MENU_TASK_STACK_WORDS];
static uint8_t s_user_pin[MENU_SETTINGS_PIN_LEN] = { 0U, 0U, 0U, 0U };
static uint8_t s_admin_pin[MENU_SETTINGS_PIN_LEN] = { 9U, 9U, 9U, 9U };
static bool s_pin_enabled = true;

static void menu_main_task_fn(void *argument);
static const menu_page_t *menu_get_page(menu_page_id_t page_id);
static void menu_render(menu_state_t *st);
static void menu_render_popup(const char *l0, const char *l1, const char *l2, const char *l3);
static void menu_render_quick_reply(menu_state_t *st);
static void menu_render_pin(menu_state_t *st);
static void menu_enter_monitor(menu_state_t *st);
static void menu_open_page(menu_state_t *st, menu_page_id_t page_id);
static void menu_open_info_modal(menu_state_t *st,
                                 const char *l0,
                                 const char *l1,
                                 const char *l2,
                                 const char *l3);
static void menu_close_modal(menu_state_t *st);
static void menu_clear_tx_ack_state(menu_state_t *st);
static void menu_handle_button(menu_state_t *st, button_event_t evt);
static void menu_handle_modal_button(menu_state_t *st, button_event_t evt);
static void menu_handle_pin_button(menu_state_t *st, button_event_t evt);
static void menu_handle_notification(menu_state_t *st, const menu_notification_t *n);
static void menu_execute_action(menu_state_t *st, menu_action_t action);
static void menu_show_action_result(menu_state_t *st, menu_notification_type_t type, const char *text);
static void menu_show_ok_or_error(menu_state_t *st, bool ok, const char *ok_text, const char *err_text);
static void menu_show_send_result(menu_state_t *st, bool ok, const char *sent_text);
static bool menu_execute_radio_action(menu_state_t *st, menu_action_t action);
static bool menu_is_send_action(menu_action_t action);
static bool menu_is_send_target_allowed(uint32_t node_id);
static bool menu_item_is_selectable(const menu_state_t *st, const menu_page_t *page, uint8_t item_idx);
static void menu_open_send_prompt(menu_state_t *st, menu_action_t action, const char *label);
static void menu_open_send_target_page(menu_state_t *st, menu_action_t action);
static bool menu_start_pairing_session(menu_state_t *st, bool send_pair_req, bool network_mode);
static void menu_open_device_delete_action(menu_state_t *st, uint8_t slot);
static bool menu_get_trusted_slot(uint8_t slot, trusted_info_t *info_out);
static bool menu_gateway_slot_in_use(void);
static uint8_t menu_line_append_device_slot_name(char *dst, uint8_t offset, uint8_t slot);
static void menu_build_trusted_slot_label(uint8_t slot, const trusted_info_t *info, char *dst);
static void menu_build_empty_trusted_slot_label(uint8_t slot, char *dst);
static bool menu_should_open_quick_reply(const char *text);
static bool menu_modal_is_preemptible(menu_modal_t modal);
static menu_auth_level_t menu_page_auth_level(menu_page_id_t page_id);
static bool menu_auth_unlocked(const menu_state_t *st, menu_auth_level_t auth_level);
static bool menu_pin_matches(const menu_state_t *st);
static void menu_request_pin(menu_state_t *st, menu_page_id_t target_page, menu_auth_level_t auth_level);
static void menu_start_pin_change(menu_state_t *st, menu_action_t action);
static bool menu_is_pin_change_action(menu_action_t action);
static void menu_clear_pin_state(menu_state_t *st);
static void menu_line_clear(char *dst);
static uint8_t menu_line_copy(char *dst, uint8_t offset, const char *src, uint8_t max_chars);
static uint8_t menu_line_append_u32(char *dst, uint8_t offset, uint32_t value);
static uint8_t menu_line_append_i32(char *dst, uint8_t offset, int32_t value);
static uint8_t menu_line_append_hex32(char *dst, uint8_t offset, uint32_t value);
static uint8_t menu_line_append_freq_mhz(char *dst, uint8_t offset, uint32_t freq_hz);
static void menu_line_format_u32(char *dst, const char *prefix, uint32_t value, const char *suffix);
static void menu_line_format_i32(char *dst, const char *prefix, int32_t value, const char *suffix);
static void menu_line_format_hex32(char *dst, const char *prefix, uint32_t value);
static void menu_line_format_device_ref(char *dst, const char *prefix, uint32_t device_code);
static void menu_line_format_source(char *dst, uint32_t device_code);
static void menu_line_format_freq(char *dst, const char *prefix, uint32_t freq_hz);
static void menu_line_format_fixed2(char *dst, const char *prefix, float value, const char *suffix);
static void menu_line_copy_or_default(char *dst, const char *text, const char *fallback);
static const char *menu_pair_code_text(const char *text, uint8_t prefix_len);
static const char *menu_lora_bw_text(radio_lora_bw_t bw);
static const char *menu_lora_cr_text(uint8_t denominator);
static const char *menu_fsk_shape_text(radio_main_fsk_shaping_t shaping);
static const char *menu_filter_text(radio_main_filter_t filter);
static const char *menu_crc_text(radio_main_crc_type_t crc_type);
static const char *menu_ook_threshold_text(radio_main_ook_threshold_t threshold);
static const char *menu_address_filter_text(radio_main_address_filter_t filter);
static void menu_build_page_title(const menu_state_t *st, const menu_page_t *page, char *dst);
static void menu_build_item_label(const menu_state_t *st,
                                  const menu_page_t *page,
                                  uint8_t item_idx,
                                  char *dst);
static uint8_t menu_radio_settings_item_count(const radio_main_runtime_cfg_t *cfg);
static void menu_build_radio_settings_item(const radio_main_runtime_cfg_t *cfg,
                                           uint8_t item_idx,
                                           char *dst);

static const menu_item_t s_page_pager_items[] =
{
    { "Radio settings", MENU_PAGE_RADIO_SETTINGS, MENU_ACTION_NONE },
    { "Send message", MENU_PAGE_SEND_OPTIONS, MENU_ACTION_NONE },
    { "Message groups", MENU_PAGE_MSG_GROUPS, MENU_ACTION_NONE },
    { "Main menu", MENU_PAGE_MAIN, MENU_ACTION_NONE },
    { "Exit", MENU_PAGE_NONE, MENU_ACTION_EXIT_TO_MONITOR }
};

static const menu_item_t s_page_radio_settings_items[] =
{
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_send_options_items[] =
{
    { "STS:OK", MENU_PAGE_NONE, MENU_ACTION_SEND_DEFAULT },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_send_direct_list_items[] =
{
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_send_target_list_items[] =
{
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_msg_groups_items[] =
{
    { "ALERT", MENU_PAGE_GROUP_ALERT, MENU_ACTION_NONE },
    { "STATUS", MENU_PAGE_GROUP_STATUS, MENU_ACTION_NONE },
    { "SERVICE", MENU_PAGE_GROUP_SERVICE, MENU_ACTION_NONE },
    { "ASK/ACT", MENU_PAGE_GROUP_QUICK, MENU_ACTION_NONE },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_group_alert_items[] =
{
    { "ALR:FIRE", MENU_PAGE_NONE, MENU_ACTION_SEND_ALERT_FIRE },
    { "ALR:INTRUSION", MENU_PAGE_NONE, MENU_ACTION_SEND_ALERT_INTR },
    { "ALR:LOWBATT", MENU_PAGE_NONE, MENU_ACTION_SEND_ALERT_LOWBATT },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_group_status_items[] =
{
    { "STS:OK", MENU_PAGE_NONE, MENU_ACTION_SEND_STATUS_OK },
    { "STS:BUSY", MENU_PAGE_NONE, MENU_ACTION_SEND_STATUS_BUSY },
    { "STS:IDLE", MENU_PAGE_NONE, MENU_ACTION_SEND_STATUS_IDLE },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_group_service_items[] =
{
    { "SRV:PING", MENU_PAGE_NONE, MENU_ACTION_SEND_SERVICE_PING },
    { "SRV:RESET", MENU_PAGE_NONE, MENU_ACTION_SEND_SERVICE_RESET },
    { "SRV:SYNC", MENU_PAGE_NONE, MENU_ACTION_SEND_SERVICE_SYNC },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_group_quick_items[] =
{
    { "ASK: Is done?", MENU_PAGE_NONE, MENU_ACTION_SEND_ASK_DONE },
    { "ACT: Come over.", MENU_PAGE_NONE, MENU_ACTION_SEND_ACT_COME_OVER },
    { "ACT: Stop!", MENU_PAGE_NONE, MENU_ACTION_SEND_ACT_STOP },
    { "ASK: Is ready?", MENU_PAGE_NONE, MENU_ACTION_SEND_ASK_READY },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_main_items[] =
{
    { "Devices", MENU_PAGE_DEVICES, MENU_ACTION_NONE },
    { "Security", MENU_PAGE_SECURITY, MENU_ACTION_NONE },
    { "Hardware", MENU_PAGE_HARDWARE, MENU_ACTION_NONE },
    { "Modulation", MENU_PAGE_MODULATION, MENU_ACTION_NONE },
    { "Info", MENU_PAGE_INFO, MENU_ACTION_NONE },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_pin_settings_items[] =
{
    { "Toggle PIN", MENU_PAGE_NONE, MENU_ACTION_PIN_TOGGLE },
    { "Change user PIN", MENU_PAGE_NONE, MENU_ACTION_PIN_SET_USER },
    { "Change admin PIN", MENU_PAGE_NONE, MENU_ACTION_PIN_SET_ADMIN },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_devices_items[] =
{
    { "Add new device", MENU_PAGE_NONE, MENU_ACTION_DEVICE_ADD },
    { "Pair with network", MENU_PAGE_NONE, MENU_ACTION_DEVICE_ADD_NETWORK },
    { "Delete device", MENU_PAGE_DEVICE_DELETE_LIST, MENU_ACTION_NONE },
    { "Connection info", MENU_PAGE_NONE, MENU_ACTION_DEVICE_INFO },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_device_delete_list_items[] =
{
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_device_delete_action_items[] =
{
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "Wyrzuc", MENU_PAGE_NONE, MENU_ACTION_DEVICE_DELETE_CONFIRM },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_security_items[] =
{
    { "PIN settings", MENU_PAGE_PIN_SETTINGS, MENU_ACTION_NONE },
    { "Frequency hopping", MENU_PAGE_SECURITY_FH, MENU_ACTION_NONE },
    { "TPM", MENU_PAGE_NONE, MENU_ACTION_SEC_TPM_INFO },
    { "Keys", MENU_PAGE_NONE, MENU_ACTION_SEC_ROTATE_KEYS },
    { "Coding", MENU_PAGE_SECURITY_CODING, MENU_ACTION_NONE },
    { "Notif mode", MENU_PAGE_SECURITY_NOTIFY, MENU_ACTION_NONE },
    { "Auto ping", MENU_PAGE_SECURITY_AUTOPING, MENU_ACTION_NONE },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_security_fh_items[] =
{
    { "Enable FH", MENU_PAGE_NONE, MENU_ACTION_SEC_FH_ENABLE },
    { "Disable FH", MENU_PAGE_NONE, MENU_ACTION_SEC_FH_DISABLE },
    { "Cycle 1 sec", MENU_PAGE_NONE, MENU_ACTION_SEC_FH_PERIOD_1000 },
    { "Cycle 2 sec", MENU_PAGE_NONE, MENU_ACTION_SEC_FH_PERIOD_2000 },
    { "Cycle 5 sec", MENU_PAGE_NONE, MENU_ACTION_SEC_FH_PERIOD_5000 },
    { "Cycle 10 sec", MENU_PAGE_NONE, MENU_ACTION_SEC_FH_PERIOD_10000 },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_security_coding_items[] =
{
    { "Coding ON", MENU_PAGE_NONE, MENU_ACTION_SEC_CODING_ON },
    { "Coding OFF", MENU_PAGE_NONE, MENU_ACTION_SEC_CODING_OFF },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_security_notify_items[] =
{
    { "Popup", MENU_PAGE_NONE, MENU_ACTION_SEC_NOTIFY_POPUP },
    { "Badge", MENU_PAGE_NONE, MENU_ACTION_SEC_NOTIFY_BADGE },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_security_autoping_items[] =
{
    { "", MENU_PAGE_NONE, MENU_ACTION_NONE },
    { "Auto ping ON", MENU_PAGE_NONE, MENU_ACTION_SEC_AUTOPING_ON },
    { "Auto ping OFF", MENU_PAGE_NONE, MENU_ACTION_SEC_AUTOPING_OFF },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_hardware_items[] =
{
    { "Dist measure", MENU_PAGE_NONE, MENU_ACTION_HW_MEASURE_DIST },
    { "Temperature", MENU_PAGE_NONE, MENU_ACTION_HW_MEASURE_TEMP },
    { "Pressure", MENU_PAGE_NONE, MENU_ACTION_HW_MEASURE_PRESS },
    { "Led", MENU_PAGE_HARDWARE_LED, MENU_ACTION_NONE },
    { "Reset SX1276", MENU_PAGE_NONE, MENU_ACTION_HW_RADIO_RESET },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_hardware_led_items[] =
{
    { "Rainbow", MENU_PAGE_NONE, MENU_ACTION_HW_LED_RAINBOW },
    { "Breath", MENU_PAGE_NONE, MENU_ACTION_HW_LED_BREATH },
    { "Off", MENU_PAGE_NONE, MENU_ACTION_HW_LED_OFF },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_modulation_items[] =
{
    { "LoRa", MENU_PAGE_MOD_LORA, MENU_ACTION_NONE },
    { "FSK/GxSK", MENU_PAGE_MOD_FSK, MENU_ACTION_NONE },
    { "OOK", MENU_PAGE_MOD_OOK, MENU_ACTION_NONE },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_lora_items[] =
{
    { "Use LoRa", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_ENABLE },
    { "Reset defaults", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_RESET },
    { "Frequency", MENU_PAGE_MOD_LORA_FREQ, MENU_ACTION_NONE },
    { "Bandwidth", MENU_PAGE_MOD_LORA_BW, MENU_ACTION_NONE },
    { "Spread factor", MENU_PAGE_MOD_LORA_SF, MENU_ACTION_NONE },
    { "Coding rate", MENU_PAGE_MOD_LORA_CR, MENU_ACTION_NONE },
    { "TX power", MENU_PAGE_MOD_LORA_POWER, MENU_ACTION_NONE },
    { "CRC", MENU_PAGE_MOD_LORA_CRC, MENU_ACTION_NONE },
    { "Preamble", MENU_PAGE_MOD_LORA_PREAMBLE, MENU_ACTION_NONE },
    { "Header mode", MENU_PAGE_MOD_LORA_HEADER, MENU_ACTION_NONE },
    { "I/Q invert", MENU_PAGE_MOD_LORA_IQ, MENU_ACTION_NONE },
    { "Sync word", MENU_PAGE_MOD_LORA_SYNC, MENU_ACTION_NONE },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_lora_freq_items[] =
{
    { "868.0 MHz", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_FREQ_8680 },
    { "868.1 MHz", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_FREQ_8681 },
    { "868.3 MHz", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_FREQ_8683 },
    { "868.5 MHz", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_FREQ_8685 },
    { "868.8 MHz", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_FREQ_8688 },
    { "869.05 MHz", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_FREQ_86905 },
    { "869.525 MHz", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_FREQ_869525 },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_lora_bw_items[] =
{
    { "BW 7.8 kHz", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_BW_7_8 },
    { "BW 10.4 kHz", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_BW_10_4 },
    { "BW 15.6 kHz", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_BW_15_6 },
    { "BW 20.8 kHz", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_BW_20_8 },
    { "BW 31.25 kHz", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_BW_31_25 },
    { "BW 41.7 kHz", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_BW_41_7 },
    { "BW 62.5 kHz", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_BW_62_5 },
    { "BW 125 kHz", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_BW_125 },
    { "BW 250 kHz", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_BW_250 },
    { "BW 500 kHz", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_BW_500 },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_lora_sf_items[] =
{
    { "SF6", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_SF_6 },
    { "SF7", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_SF_7 },
    { "SF8", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_SF_8 },
    { "SF9", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_SF_9 },
    { "SF10", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_SF_10 },
    { "SF11", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_SF_11 },
    { "SF12", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_SF_12 },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_lora_cr_items[] =
{
    { "CR 4/5", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_CR_45 },
    { "CR 4/6", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_CR_46 },
    { "CR 4/7", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_CR_47 },
    { "CR 4/8", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_CR_48 },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_lora_power_items[] =
{
    { "2 dBm", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_PWR_2 },
    { "5 dBm", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_PWR_5 },
    { "8 dBm", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_PWR_8 },
    { "11 dBm", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_PWR_11 },
    { "14 dBm", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_PWR_14 },
    { "17 dBm", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_PWR_17 },
    { "20 dBm", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_PWR_20 },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_lora_crc_items[] =
{
    { "CRC OFF", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_CRC_OFF },
    { "CRC ON", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_CRC_ON },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_lora_preamble_items[] =
{
    { "6 sym", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_PREAMBLE_6 },
    { "8 sym", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_PREAMBLE_8 },
    { "12 sym", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_PREAMBLE_12 },
    { "16 sym", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_PREAMBLE_16 },
    { "24 sym", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_PREAMBLE_24 },
    { "32 sym", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_PREAMBLE_32 },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_lora_header_items[] =
{
    { "Explicit", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_HEADER_EXPLICIT },
    { "Implicit", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_HEADER_IMPLICIT },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_lora_iq_items[] =
{
    { "Normal", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_IQ_NORMAL },
    { "Invert", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_IQ_INVERT },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_lora_sync_items[] =
{
    { "0x12 private", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_SYNC_12 },
    { "0x34 public", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_SYNC_34 },
    { "0x56 custom", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_SYNC_56 },
    { "0xA5 custom", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_SYNC_A5 },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_fsk_items[] =
{
    { "Use FSK/GxSK", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_ENABLE },
    { "Reset defaults", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_RESET },
    { "Shaping", MENU_PAGE_MOD_FSK_SHAPING, MENU_ACTION_NONE },
    { "Frequency", MENU_PAGE_MOD_FSK_FREQ, MENU_ACTION_NONE },
    { "Bitrate", MENU_PAGE_MOD_FSK_BITRATE, MENU_ACTION_NONE },
    { "RX bandwidth", MENU_PAGE_MOD_FSK_BW, MENU_ACTION_NONE },
    { "Filter / BT", MENU_PAGE_MOD_FSK_FILTER, MENU_ACTION_NONE },
    { "TX power", MENU_PAGE_MOD_FSK_POWER, MENU_ACTION_NONE },
    { "Preamble", MENU_PAGE_MOD_FSK_PREAMBLE, MENU_ACTION_NONE },
    { "Sync length", MENU_PAGE_MOD_FSK_SYNC_LEN, MENU_ACTION_NONE },
    { "Sync word", MENU_PAGE_MOD_FSK_SYNC_WORD, MENU_ACTION_NONE },
    { "Address mode", MENU_PAGE_MOD_FSK_ADDR, MENU_ACTION_NONE },
    { "CRC", MENU_PAGE_MOD_FSK_CRC, MENU_ACTION_NONE },
    { "Whitening", MENU_PAGE_MOD_FSK_WHITEN, MENU_ACTION_NONE },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_fsk_shaping_items[] =
{
    { "FSK", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_SHAPING_FSK },
    { "GFSK", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_SHAPING_GFSK },
    { "MSK", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_SHAPING_MSK },
    { "GMSK", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_SHAPING_GMSK },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_fsk_freq_items[] =
{
    { "868.0 MHz", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_FREQ_8680 },
    { "868.1 MHz", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_FREQ_8681 },
    { "868.3 MHz", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_FREQ_8683 },
    { "868.5 MHz", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_FREQ_8685 },
    { "868.8 MHz", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_FREQ_8688 },
    { "869.05 MHz", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_FREQ_86905 },
    { "869.525 MHz", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_FREQ_869525 },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_fsk_bitrate_items[] =
{
    { "1.2 kbps", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_BITRATE_1200 },
    { "2.4 kbps", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_BITRATE_2400 },
    { "4.8 kbps", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_BITRATE_4800 },
    { "9.6 kbps", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_BITRATE_9600 },
    { "19.2 kbps", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_BITRATE_19200 },
    { "38.4 kbps", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_BITRATE_38400 },
    { "50.0 kbps", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_BITRATE_50000 },
    { "100 kbps", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_BITRATE_100000 },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_fsk_bw_items[] =
{
    { "BW 31.25 kHz", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_BW_31_25 },
    { "BW 62.5 kHz", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_BW_62_5 },
    { "BW 125 kHz", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_BW_125 },
    { "BW 250 kHz", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_BW_250 },
    { "BW 500 kHz", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_BW_500 },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_fsk_filter_items[] =
{
    { "No filter", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_FILTER_NONE },
    { "BT = 1.0", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_FILTER_BT10 },
    { "BT = 0.7", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_FILTER_BT07 },
    { "BT = 0.5", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_FILTER_BT05 },
    { "BT = 0.3", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_FILTER_BT03 },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_fsk_power_items[] =
{
    { "2 dBm", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_PWR_2 },
    { "5 dBm", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_PWR_5 },
    { "8 dBm", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_PWR_8 },
    { "11 dBm", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_PWR_11 },
    { "14 dBm", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_PWR_14 },
    { "17 dBm", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_PWR_17 },
    { "20 dBm", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_PWR_20 },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_fsk_preamble_items[] =
{
    { "4 bytes", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_PREAMBLE_4 },
    { "8 bytes", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_PREAMBLE_8 },
    { "12 bytes", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_PREAMBLE_12 },
    { "16 bytes", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_PREAMBLE_16 },
    { "24 bytes", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_PREAMBLE_24 },
    { "32 bytes", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_PREAMBLE_32 },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_fsk_sync_len_items[] =
{
    { "0 byte", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_SYNC_LEN_0 },
    { "1 byte", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_SYNC_LEN_1 },
    { "2 bytes", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_SYNC_LEN_2 },
    { "3 bytes", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_SYNC_LEN_3 },
    { "4 bytes", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_SYNC_LEN_4 },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_fsk_sync_word_items[] =
{
    { "0x55AA", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_SYNC_WORD_55AA },
    { "0x2DD4", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_SYNC_WORD_2DD4 },
    { "0xA55A", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_SYNC_WORD_A55A },
    { "0xC194C1", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_SYNC_WORD_C194C1 },
    { "0x1ACFFC1D", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_SYNC_WORD_1ACFFC1D },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_fsk_addr_items[] =
{
    { "No filter", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_ADDR_NONE },
    { "Node only", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_ADDR_NODE },
    { "Node + BC", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_ADDR_NODE_BC },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_fsk_crc_items[] =
{
    { "CRC OFF", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_CRC_OFF },
    { "CRC IBM", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_CRC_IBM },
    { "CRC CCITT", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_CRC_CCITT },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_fsk_whiten_items[] =
{
    { "Whitening OFF", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_WHITEN_OFF },
    { "Whitening ON", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_WHITEN_ON },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_ook_items[] =
{
    { "Use OOK mode", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_ENABLE },
    { "Reset defaults", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_RESET },
    { "Frequency", MENU_PAGE_MOD_OOK_FREQ, MENU_ACTION_NONE },
    { "Bitrate", MENU_PAGE_MOD_OOK_BITRATE, MENU_ACTION_NONE },
    { "RX bandwidth", MENU_PAGE_MOD_OOK_BW, MENU_ACTION_NONE },
    { "TX power", MENU_PAGE_MOD_OOK_POWER, MENU_ACTION_NONE },
    { "Preamble", MENU_PAGE_MOD_OOK_PREAMBLE, MENU_ACTION_NONE },
    { "Sync len", MENU_PAGE_MOD_OOK_SYNC_LEN, MENU_ACTION_NONE },
    { "Sync word", MENU_PAGE_MOD_OOK_SYNC_WORD, MENU_ACTION_NONE },
    { "Threshold type", MENU_PAGE_MOD_OOK_THRESH_TYPE, MENU_ACTION_NONE },
    { "Threshold val", MENU_PAGE_MOD_OOK_THRESH_VALUE, MENU_ACTION_NONE },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_ook_freq_items[] =
{
    { "868.0 MHz", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_FREQ_8680 },
    { "868.1 MHz", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_FREQ_8681 },
    { "868.3 MHz", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_FREQ_8683 },
    { "868.5 MHz", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_FREQ_8685 },
    { "868.8 MHz", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_FREQ_8688 },
    { "869.05 MHz", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_FREQ_86905 },
    { "869.525 MHz", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_FREQ_869525 },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_ook_bitrate_items[] =
{
    { "1.2 kbps", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_BITRATE_1200 },
    { "2.4 kbps", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_BITRATE_2400 },
    { "4.8 kbps", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_BITRATE_4800 },
    { "9.6 kbps", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_BITRATE_9600 },
    { "19.2 kbps", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_BITRATE_19200 },
    { "38.4 kbps", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_BITRATE_38400 },
    { "50.0 kbps", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_BITRATE_50000 },
    { "100 kbps", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_BITRATE_100000 },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_ook_power_items[] =
{
    { "2 dBm", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_PWR_2 },
    { "5 dBm", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_PWR_5 },
    { "8 dBm", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_PWR_8 },
    { "11 dBm", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_PWR_11 },
    { "14 dBm", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_PWR_14 },
    { "17 dBm", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_PWR_17 },
    { "20 dBm", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_PWR_20 },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_ook_bw_items[] =
{
    { "BW 31.25 kHz", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_BW_31_25 },
    { "BW 62.5 kHz", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_BW_62_5 },
    { "BW 125 kHz", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_BW_125 },
    { "BW 250 kHz", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_BW_250 },
    { "BW 500 kHz", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_BW_500 },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_ook_preamble_items[] =
{
    { "4 bytes", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_PREAMBLE_4 },
    { "8 bytes", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_PREAMBLE_8 },
    { "12 bytes", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_PREAMBLE_12 },
    { "16 bytes", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_PREAMBLE_16 },
    { "24 bytes", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_PREAMBLE_24 },
    { "32 bytes", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_PREAMBLE_32 },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_ook_sync_len_items[] =
{
    { "0 byte", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_SYNC_LEN_0 },
    { "1 byte", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_SYNC_LEN_1 },
    { "2 bytes", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_SYNC_LEN_2 },
    { "3 bytes", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_SYNC_LEN_3 },
    { "4 bytes", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_SYNC_LEN_4 },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_ook_sync_word_items[] =
{
    { "0x55AA", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_SYNC_WORD_55AA },
    { "0x2DD4", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_SYNC_WORD_2DD4 },
    { "0xA55A", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_SYNC_WORD_A55A },
    { "0xC194C1", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_SYNC_WORD_C194C1 },
    { "0x1ACFFC1D", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_SYNC_WORD_1ACFFC1D },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_ook_thresh_type_items[] =
{
    { "Fixed", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_THRESH_FIXED },
    { "Peak", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_THRESH_PEAK },
    { "Average", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_THRESH_AVG },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_ook_thresh_value_items[] =
{
    { "Thr = 4", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_THRESH_4 },
    { "Thr = 8", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_THRESH_8 },
    { "Thr = 12", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_THRESH_12 },
    { "Thr = 16", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_THRESH_16 },
    { "Thr = 24", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_THRESH_24 },
    { "Thr = 32", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_THRESH_32 },
    { "Thr = 48", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_THRESH_48 },
    { "Thr = 64", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_THRESH_64 },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_info_items[] =
{
    { "Show info", MENU_PAGE_NONE, MENU_ACTION_INFO_SHOW },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_page_t s_pages[] =
{
    { "PAGER", MENU_PAGE_NONE, s_page_pager_items, (uint8_t)(sizeof(s_page_pager_items) / sizeof(s_page_pager_items[0])) },
    { "RADIO SETTINGS", MENU_PAGE_PAGER, s_page_radio_settings_items, (uint8_t)(sizeof(s_page_radio_settings_items) / sizeof(s_page_radio_settings_items[0])) },
    { "SEND", MENU_PAGE_PAGER, s_page_send_options_items, (uint8_t)(sizeof(s_page_send_options_items) / sizeof(s_page_send_options_items[0])) },
    { "SEND DIRECT", MENU_PAGE_SEND_OPTIONS, s_page_send_direct_list_items, (uint8_t)(sizeof(s_page_send_direct_list_items) / sizeof(s_page_send_direct_list_items[0])) },
    { "SEND TO", MENU_PAGE_SEND_OPTIONS, s_page_send_target_list_items, (uint8_t)(sizeof(s_page_send_target_list_items) / sizeof(s_page_send_target_list_items[0])) },
    { "MSG GROUPS", MENU_PAGE_PAGER, s_page_msg_groups_items, (uint8_t)(sizeof(s_page_msg_groups_items) / sizeof(s_page_msg_groups_items[0])) },
    { "ALERT", MENU_PAGE_MSG_GROUPS, s_page_group_alert_items, (uint8_t)(sizeof(s_page_group_alert_items) / sizeof(s_page_group_alert_items[0])) },
    { "STATUS", MENU_PAGE_MSG_GROUPS, s_page_group_status_items, (uint8_t)(sizeof(s_page_group_status_items) / sizeof(s_page_group_status_items[0])) },
    { "SERVICE", MENU_PAGE_MSG_GROUPS, s_page_group_service_items, (uint8_t)(sizeof(s_page_group_service_items) / sizeof(s_page_group_service_items[0])) },
    { "ASK/ACT", MENU_PAGE_MSG_GROUPS, s_page_group_quick_items, (uint8_t)(sizeof(s_page_group_quick_items) / sizeof(s_page_group_quick_items[0])) },
    { "MAIN MENU", MENU_PAGE_PAGER, s_page_main_items, (uint8_t)(sizeof(s_page_main_items) / sizeof(s_page_main_items[0])) },
    { "DEVICES", MENU_PAGE_MAIN, s_page_devices_items, (uint8_t)(sizeof(s_page_devices_items) / sizeof(s_page_devices_items[0])) },
    { "DELETE DEV", MENU_PAGE_DEVICES, s_page_device_delete_list_items, (uint8_t)(sizeof(s_page_device_delete_list_items) / sizeof(s_page_device_delete_list_items[0])) },
    { "DELETE DEV", MENU_PAGE_DEVICE_DELETE_LIST, s_page_device_delete_action_items, (uint8_t)(sizeof(s_page_device_delete_action_items) / sizeof(s_page_device_delete_action_items[0])) },
    { "PIN SETTINGS", MENU_PAGE_SECURITY, s_page_pin_settings_items, (uint8_t)(sizeof(s_page_pin_settings_items) / sizeof(s_page_pin_settings_items[0])) },
    { "SECURITY", MENU_PAGE_MAIN, s_page_security_items, (uint8_t)(sizeof(s_page_security_items) / sizeof(s_page_security_items[0])) },
    { "SEC FH", MENU_PAGE_SECURITY, s_page_security_fh_items, (uint8_t)(sizeof(s_page_security_fh_items) / sizeof(s_page_security_fh_items[0])) },
    { "CODING", MENU_PAGE_SECURITY, s_page_security_coding_items, (uint8_t)(sizeof(s_page_security_coding_items) / sizeof(s_page_security_coding_items[0])) },
    { "NOTIFY", MENU_PAGE_SECURITY, s_page_security_notify_items, (uint8_t)(sizeof(s_page_security_notify_items) / sizeof(s_page_security_notify_items[0])) },
    { "AUTOPING", MENU_PAGE_SECURITY, s_page_security_autoping_items, (uint8_t)(sizeof(s_page_security_autoping_items) / sizeof(s_page_security_autoping_items[0])) },
    { "HARDWARE", MENU_PAGE_MAIN, s_page_hardware_items, (uint8_t)(sizeof(s_page_hardware_items) / sizeof(s_page_hardware_items[0])) },
    { "LED", MENU_PAGE_HARDWARE, s_page_hardware_led_items, (uint8_t)(sizeof(s_page_hardware_led_items) / sizeof(s_page_hardware_led_items[0])) },
    { "MODULATION", MENU_PAGE_MAIN, s_page_modulation_items, (uint8_t)(sizeof(s_page_modulation_items) / sizeof(s_page_modulation_items[0])) },
    { "LORA", MENU_PAGE_MODULATION, s_page_mod_lora_items, (uint8_t)(sizeof(s_page_mod_lora_items) / sizeof(s_page_mod_lora_items[0])) },
    { "LORA FREQ", MENU_PAGE_MOD_LORA, s_page_mod_lora_freq_items, (uint8_t)(sizeof(s_page_mod_lora_freq_items) / sizeof(s_page_mod_lora_freq_items[0])) },
    { "LORA BW", MENU_PAGE_MOD_LORA, s_page_mod_lora_bw_items, (uint8_t)(sizeof(s_page_mod_lora_bw_items) / sizeof(s_page_mod_lora_bw_items[0])) },
    { "LORA SF", MENU_PAGE_MOD_LORA, s_page_mod_lora_sf_items, (uint8_t)(sizeof(s_page_mod_lora_sf_items) / sizeof(s_page_mod_lora_sf_items[0])) },
    { "LORA CR", MENU_PAGE_MOD_LORA, s_page_mod_lora_cr_items, (uint8_t)(sizeof(s_page_mod_lora_cr_items) / sizeof(s_page_mod_lora_cr_items[0])) },
    { "LORA PWR", MENU_PAGE_MOD_LORA, s_page_mod_lora_power_items, (uint8_t)(sizeof(s_page_mod_lora_power_items) / sizeof(s_page_mod_lora_power_items[0])) },
    { "LORA CRC", MENU_PAGE_MOD_LORA, s_page_mod_lora_crc_items, (uint8_t)(sizeof(s_page_mod_lora_crc_items) / sizeof(s_page_mod_lora_crc_items[0])) },
    { "LORA PREAM", MENU_PAGE_MOD_LORA, s_page_mod_lora_preamble_items, (uint8_t)(sizeof(s_page_mod_lora_preamble_items) / sizeof(s_page_mod_lora_preamble_items[0])) },
    { "LORA HEADER", MENU_PAGE_MOD_LORA, s_page_mod_lora_header_items, (uint8_t)(sizeof(s_page_mod_lora_header_items) / sizeof(s_page_mod_lora_header_items[0])) },
    { "LORA IQ", MENU_PAGE_MOD_LORA, s_page_mod_lora_iq_items, (uint8_t)(sizeof(s_page_mod_lora_iq_items) / sizeof(s_page_mod_lora_iq_items[0])) },
    { "LORA SYNC", MENU_PAGE_MOD_LORA, s_page_mod_lora_sync_items, (uint8_t)(sizeof(s_page_mod_lora_sync_items) / sizeof(s_page_mod_lora_sync_items[0])) },
    { "FSK", MENU_PAGE_MODULATION, s_page_mod_fsk_items, (uint8_t)(sizeof(s_page_mod_fsk_items) / sizeof(s_page_mod_fsk_items[0])) },
    { "FSK SHAPE", MENU_PAGE_MOD_FSK, s_page_mod_fsk_shaping_items, (uint8_t)(sizeof(s_page_mod_fsk_shaping_items) / sizeof(s_page_mod_fsk_shaping_items[0])) },
    { "FSK FREQ", MENU_PAGE_MOD_FSK, s_page_mod_fsk_freq_items, (uint8_t)(sizeof(s_page_mod_fsk_freq_items) / sizeof(s_page_mod_fsk_freq_items[0])) },
    { "FSK BITR", MENU_PAGE_MOD_FSK, s_page_mod_fsk_bitrate_items, (uint8_t)(sizeof(s_page_mod_fsk_bitrate_items) / sizeof(s_page_mod_fsk_bitrate_items[0])) },
    { "FSK BW", MENU_PAGE_MOD_FSK, s_page_mod_fsk_bw_items, (uint8_t)(sizeof(s_page_mod_fsk_bw_items) / sizeof(s_page_mod_fsk_bw_items[0])) },
    { "FSK FILTER", MENU_PAGE_MOD_FSK, s_page_mod_fsk_filter_items, (uint8_t)(sizeof(s_page_mod_fsk_filter_items) / sizeof(s_page_mod_fsk_filter_items[0])) },
    { "FSK PWR", MENU_PAGE_MOD_FSK, s_page_mod_fsk_power_items, (uint8_t)(sizeof(s_page_mod_fsk_power_items) / sizeof(s_page_mod_fsk_power_items[0])) },
    { "FSK PREAM", MENU_PAGE_MOD_FSK, s_page_mod_fsk_preamble_items, (uint8_t)(sizeof(s_page_mod_fsk_preamble_items) / sizeof(s_page_mod_fsk_preamble_items[0])) },
    { "FSK SLEN", MENU_PAGE_MOD_FSK, s_page_mod_fsk_sync_len_items, (uint8_t)(sizeof(s_page_mod_fsk_sync_len_items) / sizeof(s_page_mod_fsk_sync_len_items[0])) },
    { "FSK SYNC", MENU_PAGE_MOD_FSK, s_page_mod_fsk_sync_word_items, (uint8_t)(sizeof(s_page_mod_fsk_sync_word_items) / sizeof(s_page_mod_fsk_sync_word_items[0])) },
    { "FSK ADDR", MENU_PAGE_MOD_FSK, s_page_mod_fsk_addr_items, (uint8_t)(sizeof(s_page_mod_fsk_addr_items) / sizeof(s_page_mod_fsk_addr_items[0])) },
    { "FSK CRC", MENU_PAGE_MOD_FSK, s_page_mod_fsk_crc_items, (uint8_t)(sizeof(s_page_mod_fsk_crc_items) / sizeof(s_page_mod_fsk_crc_items[0])) },
    { "FSK WHITE", MENU_PAGE_MOD_FSK, s_page_mod_fsk_whiten_items, (uint8_t)(sizeof(s_page_mod_fsk_whiten_items) / sizeof(s_page_mod_fsk_whiten_items[0])) },
    { "OOK", MENU_PAGE_MODULATION, s_page_mod_ook_items, (uint8_t)(sizeof(s_page_mod_ook_items) / sizeof(s_page_mod_ook_items[0])) },
    { "OOK FREQ", MENU_PAGE_MOD_OOK, s_page_mod_ook_freq_items, (uint8_t)(sizeof(s_page_mod_ook_freq_items) / sizeof(s_page_mod_ook_freq_items[0])) },
    { "OOK BITR", MENU_PAGE_MOD_OOK, s_page_mod_ook_bitrate_items, (uint8_t)(sizeof(s_page_mod_ook_bitrate_items) / sizeof(s_page_mod_ook_bitrate_items[0])) },
    { "OOK PWR", MENU_PAGE_MOD_OOK, s_page_mod_ook_power_items, (uint8_t)(sizeof(s_page_mod_ook_power_items) / sizeof(s_page_mod_ook_power_items[0])) },
    { "OOK BW", MENU_PAGE_MOD_OOK, s_page_mod_ook_bw_items, (uint8_t)(sizeof(s_page_mod_ook_bw_items) / sizeof(s_page_mod_ook_bw_items[0])) },
    { "OOK PRE", MENU_PAGE_MOD_OOK, s_page_mod_ook_preamble_items, (uint8_t)(sizeof(s_page_mod_ook_preamble_items) / sizeof(s_page_mod_ook_preamble_items[0])) },
    { "OOK SYNL", MENU_PAGE_MOD_OOK, s_page_mod_ook_sync_len_items, (uint8_t)(sizeof(s_page_mod_ook_sync_len_items) / sizeof(s_page_mod_ook_sync_len_items[0])) },
    { "OOK SYNW", MENU_PAGE_MOD_OOK, s_page_mod_ook_sync_word_items, (uint8_t)(sizeof(s_page_mod_ook_sync_word_items) / sizeof(s_page_mod_ook_sync_word_items[0])) },
    { "OOK THR T", MENU_PAGE_MOD_OOK, s_page_mod_ook_thresh_type_items, (uint8_t)(sizeof(s_page_mod_ook_thresh_type_items) / sizeof(s_page_mod_ook_thresh_type_items[0])) },
    { "OOK THR V", MENU_PAGE_MOD_OOK, s_page_mod_ook_thresh_value_items, (uint8_t)(sizeof(s_page_mod_ook_thresh_value_items) / sizeof(s_page_mod_ook_thresh_value_items[0])) },
    { "INFO", MENU_PAGE_MAIN, s_page_info_items, (uint8_t)(sizeof(s_page_info_items) / sizeof(s_page_info_items[0])) }
};

#define MENU_RADIO_ACTIVATE(_action, _mod, _ok, _err) \
    { (_action), true, (_mod), false, RADIO_MAIN_OPTION_LORA_FREQ, 0UL, false, (_ok), (_err) }

#define MENU_RADIO_SET(_action, _mod, _option, _value, _ok, _err) \
    { (_action), true, (_mod), true, (_option), (_value), false, (_ok), (_err) }

#define MENU_RADIO_PRESET(_action, _preset, _ok, _err) \
    { (_action), true, RADIO_MAIN_MODULATION_LORA, true, RADIO_MAIN_OPTION_LORA_PRESET, (_preset), true, (_ok), (_err) }

static const menu_radio_action_binding_t s_radio_action_bindings[] =
{
    MENU_RADIO_ACTIVATE(MENU_ACTION_MOD_LORA_ENABLE, RADIO_MAIN_MODULATION_LORA, "LoRa mode", "LoRa set failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_RESET, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_RESET_DEFAULTS, 0UL, "LoRa reset", "LoRa reset failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_FREQ_8680, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_FREQ, 868000000UL, "LoRa F=868.0", "LoRa freq failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_FREQ_8681, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_FREQ, 868100000UL, "LoRa F=868.1", "LoRa freq failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_FREQ_8683, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_FREQ, 868300000UL, "LoRa F=868.3", "LoRa freq failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_FREQ_8685, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_FREQ, 868500000UL, "LoRa F=868.5", "LoRa freq failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_FREQ_8688, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_FREQ, 868800000UL, "LoRa F=868.8", "LoRa freq failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_FREQ_86905, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_FREQ, 869050000UL, "LoRa F=869.05", "LoRa freq failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_FREQ_869525, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_FREQ, 869525000UL, "LoRa F=869.525", "LoRa freq failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_BW_7_8, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_BW, (uint32_t)RADIO_LORA_BW_7_8_KHZ, "LoRa BW7.8", "LoRa BW failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_BW_10_4, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_BW, (uint32_t)RADIO_LORA_BW_10_4_KHZ, "LoRa BW10.4", "LoRa BW failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_BW_15_6, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_BW, (uint32_t)RADIO_LORA_BW_15_6_KHZ, "LoRa BW15.6", "LoRa BW failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_BW_20_8, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_BW, (uint32_t)RADIO_LORA_BW_20_8_KHZ, "LoRa BW20.8", "LoRa BW failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_BW_31_25, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_BW, (uint32_t)RADIO_LORA_BW_31_25_KHZ, "LoRa BW31.2", "LoRa BW failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_BW_41_7, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_BW, (uint32_t)RADIO_LORA_BW_41_7_KHZ, "LoRa BW41.7", "LoRa BW failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_BW_62_5, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_BW, (uint32_t)RADIO_LORA_BW_62_5_KHZ, "LoRa BW62.5", "LoRa BW failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_BW_125, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_BW, (uint32_t)RADIO_LORA_BW_125_KHZ, "LoRa BW125", "LoRa BW failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_BW_250, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_BW, (uint32_t)RADIO_LORA_BW_250_KHZ, "LoRa BW250", "LoRa BW failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_BW_500, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_BW, (uint32_t)RADIO_LORA_BW_500_KHZ, "LoRa BW500", "LoRa BW failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_SF_6, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_SF, 6UL, "LoRa SF6", "LoRa SF failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_SF_7, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_SF, 7UL, "LoRa SF7", "LoRa SF failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_SF_8, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_SF, 8UL, "LoRa SF8", "LoRa SF failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_SF_9, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_SF, 9UL, "LoRa SF9", "LoRa SF failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_SF_10, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_SF, 10UL, "LoRa SF10", "LoRa SF failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_SF_11, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_SF, 11UL, "LoRa SF11", "LoRa SF failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_SF_12, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_SF, 12UL, "LoRa SF12", "LoRa SF failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_CR_45, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_CR, 5UL, "LoRa CR4/5", "LoRa CR failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_CR_46, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_CR, 6UL, "LoRa CR4/6", "LoRa CR failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_CR_47, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_CR, 7UL, "LoRa CR4/7", "LoRa CR failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_CR_48, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_CR, 8UL, "LoRa CR4/8", "LoRa CR failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_PWR_2, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_TX_POWER, 2UL, "LoRa P=2", "LoRa power failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_PWR_5, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_TX_POWER, 5UL, "LoRa P=5", "LoRa power failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_PWR_8, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_TX_POWER, 8UL, "LoRa P=8", "LoRa power failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_PWR_11, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_TX_POWER, 11UL, "LoRa P=11", "LoRa power failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_PWR_14, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_TX_POWER, 14UL, "LoRa P=14", "LoRa power failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_PWR_17, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_TX_POWER, 17UL, "LoRa P=17", "LoRa power failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_PWR_20, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_TX_POWER, 20UL, "LoRa P=20", "LoRa power failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_CRC_OFF, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_CRC, (uint32_t)RADIO_MAIN_CRC_OFF, "LoRa CRC OFF", "LoRa CRC failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_CRC_ON, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_CRC, (uint32_t)RADIO_MAIN_CRC_SX1276, "LoRa CRC ON", "LoRa CRC failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_PREAMBLE_6, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_PREAMBLE, 6UL, "LoRa PRE=6", "LoRa preamble failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_PREAMBLE_8, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_PREAMBLE, 8UL, "LoRa PRE=8", "LoRa preamble failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_PREAMBLE_12, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_PREAMBLE, 12UL, "LoRa PRE=12", "LoRa preamble failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_PREAMBLE_16, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_PREAMBLE, 16UL, "LoRa PRE=16", "LoRa preamble failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_PREAMBLE_24, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_PREAMBLE, 24UL, "LoRa PRE=24", "LoRa preamble failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_PREAMBLE_32, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_PREAMBLE, 32UL, "LoRa PRE=32", "LoRa preamble failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_HEADER_EXPLICIT, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_HEADER_MODE, (uint32_t)RADIO_MAIN_HEADER_EXPLICIT, "Hdr explicit", "Header failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_HEADER_IMPLICIT, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_HEADER_MODE, (uint32_t)RADIO_MAIN_HEADER_IMPLICIT, "Hdr implicit", "Header failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_IQ_NORMAL, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_IQ_INVERT, 0UL, "IQ normal", "IQ set failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_IQ_INVERT, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_IQ_INVERT, 1UL, "IQ invert", "IQ set failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_SYNC_12, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_SYNC_WORD, 0x12UL, "Sync 0x12", "Sync set failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_SYNC_34, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_SYNC_WORD, 0x34UL, "Sync 0x34", "Sync set failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_SYNC_56, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_SYNC_WORD, 0x56UL, "Sync 0x56", "Sync set failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_LORA_SYNC_A5, RADIO_MAIN_MODULATION_LORA, RADIO_MAIN_OPTION_LORA_SYNC_WORD, 0xA5UL, "Sync 0xA5", "Sync set failed"),

    MENU_RADIO_ACTIVATE(MENU_ACTION_MOD_FSK_ENABLE, RADIO_MAIN_MODULATION_FSK, "FSK mode", "FSK set failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_SHAPING_FSK, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_SHAPING, (uint32_t)RADIO_MAIN_FSK_SHAPING_FSK, "Shape FSK", "Shape failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_SHAPING_GFSK, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_SHAPING, (uint32_t)RADIO_MAIN_FSK_SHAPING_GFSK, "Shape GFSK", "Shape failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_SHAPING_MSK, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_SHAPING, (uint32_t)RADIO_MAIN_FSK_SHAPING_MSK, "Shape MSK", "Shape failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_SHAPING_GMSK, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_SHAPING, (uint32_t)RADIO_MAIN_FSK_SHAPING_GMSK, "Shape GMSK", "Shape failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_FREQ_8680, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_FREQ, 868000000UL, "FSK F=868.0", "FSK freq failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_FREQ_8681, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_FREQ, 868100000UL, "FSK F=868.1", "FSK freq failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_FREQ_8683, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_FREQ, 868300000UL, "FSK F=868.3", "FSK freq failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_FREQ_8685, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_FREQ, 868500000UL, "FSK F=868.5", "FSK freq failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_FREQ_8688, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_FREQ, 868800000UL, "FSK F=868.8", "FSK freq failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_FREQ_86905, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_FREQ, 869050000UL, "FSK F=869.05", "FSK freq failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_FREQ_869525, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_FREQ, 869525000UL, "FSK F=869.525", "FSK freq failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_BITRATE_1200, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_BITRATE, 1200UL, "FSK BR1.2", "FSK bitrate failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_BITRATE_2400, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_BITRATE, 2400UL, "FSK BR2.4", "FSK bitrate failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_BITRATE_4800, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_BITRATE, 4800UL, "FSK BR4.8", "FSK bitrate failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_BITRATE_9600, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_BITRATE, 9600UL, "FSK BR9.6", "FSK bitrate failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_BITRATE_19200, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_BITRATE, 19200UL, "FSK BR19.2", "FSK bitrate failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_BITRATE_38400, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_BITRATE, 38400UL, "FSK BR38.4", "FSK bitrate failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_BITRATE_50000, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_BITRATE, 50000UL, "FSK BR50", "FSK bitrate failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_BITRATE_100000, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_BITRATE, 100000UL, "FSK BR100", "FSK bitrate failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_BW_31_25, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_RX_BW, (uint32_t)RADIO_LORA_BW_31_25_KHZ, "FSK BW31.2", "FSK BW failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_BW_62_5, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_RX_BW, (uint32_t)RADIO_LORA_BW_62_5_KHZ, "FSK BW62.5", "FSK BW failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_BW_125, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_RX_BW, (uint32_t)RADIO_LORA_BW_125_KHZ, "FSK BW125", "FSK BW failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_BW_250, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_RX_BW, (uint32_t)RADIO_LORA_BW_250_KHZ, "FSK BW250", "FSK BW failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_BW_500, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_RX_BW, (uint32_t)RADIO_LORA_BW_500_KHZ, "FSK BW500", "FSK BW failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_FILTER_NONE, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_FILTER, (uint32_t)RADIO_MAIN_FILTER_NONE, "Filt OFF", "Filter failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_FILTER_BT10, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_FILTER, (uint32_t)RADIO_MAIN_FILTER_BT_10, "Filt BT1.0", "Filter failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_FILTER_BT07, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_FILTER, (uint32_t)RADIO_MAIN_FILTER_BT_07, "Filt BT0.7", "Filter failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_FILTER_BT05, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_FILTER, (uint32_t)RADIO_MAIN_FILTER_BT_05, "Filt BT0.5", "Filter failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_FILTER_BT03, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_FILTER, (uint32_t)RADIO_MAIN_FILTER_BT_03, "Filt BT0.3", "Filter failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_PWR_2, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_TX_POWER, 2UL, "FSK P=2", "FSK power failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_PWR_5, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_TX_POWER, 5UL, "FSK P=5", "FSK power failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_PWR_8, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_TX_POWER, 8UL, "FSK P=8", "FSK power failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_PWR_11, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_TX_POWER, 11UL, "FSK P=11", "FSK power failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_PWR_14, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_TX_POWER, 14UL, "FSK P=14", "FSK power failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_PWR_17, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_TX_POWER, 17UL, "FSK P=17", "FSK power failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_PWR_20, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_TX_POWER, 20UL, "FSK P=20", "FSK power failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_PREAMBLE_4, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_PREAMBLE, 4UL, "FSK PRE4", "FSK preamble failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_PREAMBLE_8, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_PREAMBLE, 8UL, "FSK PRE8", "FSK preamble failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_PREAMBLE_12, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_PREAMBLE, 12UL, "FSK PRE12", "FSK preamble failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_PREAMBLE_16, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_PREAMBLE, 16UL, "FSK PRE16", "FSK preamble failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_PREAMBLE_24, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_PREAMBLE, 24UL, "FSK PRE24", "FSK preamble failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_PREAMBLE_32, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_PREAMBLE, 32UL, "FSK PRE32", "FSK preamble failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_SYNC_LEN_0, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_SYNC_LEN, 0UL, "SyncLen 0", "SyncLen failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_SYNC_LEN_1, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_SYNC_LEN, 1UL, "SyncLen 1", "SyncLen failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_SYNC_LEN_2, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_SYNC_LEN, 2UL, "SyncLen 2", "SyncLen failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_SYNC_LEN_3, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_SYNC_LEN, 3UL, "SyncLen 3", "SyncLen failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_SYNC_LEN_4, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_SYNC_LEN, 4UL, "SyncLen 4", "SyncLen failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_SYNC_WORD_55AA, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_SYNC_WORD, 0x55AAUL, "Sync 55AA", "Sync word failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_SYNC_WORD_2DD4, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_SYNC_WORD, 0x2DD4UL, "Sync 2DD4", "Sync word failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_SYNC_WORD_A55A, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_SYNC_WORD, 0xA55AUL, "Sync A55A", "Sync word failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_SYNC_WORD_C194C1, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_SYNC_WORD, 0x00C194C1UL, "Sync C194C1", "Sync word failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_SYNC_WORD_1ACFFC1D, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_SYNC_WORD, 0x1ACFFC1DUL, "Sync 1ACFFC1D", "Sync word failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_ADDR_NONE, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_ADDRESS_FILTER, (uint32_t)RADIO_MAIN_ADDRESS_FILTER_NONE, "Addr OFF", "Addr failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_ADDR_NODE, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_ADDRESS_FILTER, (uint32_t)RADIO_MAIN_ADDRESS_FILTER_NODE, "Addr NODE", "Addr failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_ADDR_NODE_BC, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_ADDRESS_FILTER, (uint32_t)RADIO_MAIN_ADDRESS_FILTER_NODE_BROADCAST, "Addr N+BC", "Addr failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_CRC_OFF, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_CRC, (uint32_t)RADIO_MAIN_CRC_OFF, "FSK CRC OFF", "FSK CRC failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_CRC_IBM, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_CRC, (uint32_t)RADIO_MAIN_CRC_IBM, "FSK CRC IBM", "FSK CRC failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_CRC_CCITT, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_CRC, (uint32_t)RADIO_MAIN_CRC_CCITT, "FSK CRC CCT", "FSK CRC failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_WHITEN_OFF, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_WHITENING, 0UL, "White OFF", "Whitening failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_WHITEN_ON, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_WHITENING, 1UL, "White ON", "Whitening failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_FSK_RESET, RADIO_MAIN_MODULATION_FSK, RADIO_MAIN_OPTION_FSK_RESET_DEFAULTS, 0UL, "FSK reset", "FSK reset failed"),

    MENU_RADIO_ACTIVATE(MENU_ACTION_MOD_OOK_ENABLE, RADIO_MAIN_MODULATION_OOK, "OOK mode", "OOK set failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_FREQ_8680, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_FREQ, 868000000UL, "OOK F=868.0", "OOK freq failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_FREQ_8681, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_FREQ, 868100000UL, "OOK F=868.1", "OOK freq failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_FREQ_8683, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_FREQ, 868300000UL, "OOK F=868.3", "OOK freq failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_FREQ_8685, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_FREQ, 868500000UL, "OOK F=868.5", "OOK freq failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_FREQ_8688, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_FREQ, 868800000UL, "OOK F=868.8", "OOK freq failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_FREQ_86905, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_FREQ, 869050000UL, "OOK F=869.05", "OOK freq failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_FREQ_869525, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_FREQ, 869525000UL, "OOK F=869.525", "OOK freq failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_BITRATE_1200, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_BITRATE, 1200UL, "OOK BR1.2", "OOK bitrate failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_BITRATE_2400, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_BITRATE, 2400UL, "OOK BR2.4", "OOK bitrate failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_BITRATE_4800, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_BITRATE, 4800UL, "OOK BR4.8", "OOK bitrate failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_BITRATE_9600, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_BITRATE, 9600UL, "OOK BR9.6", "OOK bitrate failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_BITRATE_19200, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_BITRATE, 19200UL, "OOK BR19.2", "OOK bitrate failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_BITRATE_38400, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_BITRATE, 38400UL, "OOK BR38.4", "OOK bitrate failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_BITRATE_50000, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_BITRATE, 50000UL, "OOK BR50", "OOK bitrate failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_BITRATE_100000, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_BITRATE, 100000UL, "OOK BR100", "OOK bitrate failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_PWR_2, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_TX_POWER, 2UL, "OOK P=2", "OOK power failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_PWR_5, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_TX_POWER, 5UL, "OOK P=5", "OOK power failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_PWR_8, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_TX_POWER, 8UL, "OOK P=8", "OOK power failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_PWR_11, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_TX_POWER, 11UL, "OOK P=11", "OOK power failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_PWR_14, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_TX_POWER, 14UL, "OOK P=14", "OOK power failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_PWR_17, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_TX_POWER, 17UL, "OOK P=17", "OOK power failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_PWR_20, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_TX_POWER, 20UL, "OOK P=20", "OOK power failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_BW_31_25, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_RX_BW, (uint32_t)RADIO_LORA_BW_31_25_KHZ, "OOK BW31.2", "OOK BW failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_BW_62_5, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_RX_BW, (uint32_t)RADIO_LORA_BW_62_5_KHZ, "OOK BW62.5", "OOK BW failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_BW_125, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_RX_BW, (uint32_t)RADIO_LORA_BW_125_KHZ, "OOK BW125", "OOK BW failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_BW_250, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_RX_BW, (uint32_t)RADIO_LORA_BW_250_KHZ, "OOK BW250", "OOK BW failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_BW_500, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_RX_BW, (uint32_t)RADIO_LORA_BW_500_KHZ, "OOK BW500", "OOK BW failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_PREAMBLE_4, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_PREAMBLE, 4UL, "OOK PRE4", "OOK preamble failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_PREAMBLE_8, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_PREAMBLE, 8UL, "OOK PRE8", "OOK preamble failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_PREAMBLE_12, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_PREAMBLE, 12UL, "OOK PRE12", "OOK preamble failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_PREAMBLE_16, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_PREAMBLE, 16UL, "OOK PRE16", "OOK preamble failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_PREAMBLE_24, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_PREAMBLE, 24UL, "OOK PRE24", "OOK preamble failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_PREAMBLE_32, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_PREAMBLE, 32UL, "OOK PRE32", "OOK preamble failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_SYNC_LEN_0, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_SYNC_LEN, 0UL, "OSyncL 0", "OOK sync len failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_SYNC_LEN_1, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_SYNC_LEN, 1UL, "OSyncL 1", "OOK sync len failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_SYNC_LEN_2, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_SYNC_LEN, 2UL, "OSyncL 2", "OOK sync len failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_SYNC_LEN_3, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_SYNC_LEN, 3UL, "OSyncL 3", "OOK sync len failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_SYNC_LEN_4, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_SYNC_LEN, 4UL, "OSyncL 4", "OOK sync len failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_SYNC_WORD_55AA, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_SYNC_WORD, 0x55AAUL, "OSync 55AA", "OOK sync word failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_SYNC_WORD_2DD4, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_SYNC_WORD, 0x2DD4UL, "OSync 2DD4", "OOK sync word failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_SYNC_WORD_A55A, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_SYNC_WORD, 0xA55AUL, "OSync A55A", "OOK sync word failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_SYNC_WORD_C194C1, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_SYNC_WORD, 0x00C194C1UL, "OSync C194", "OOK sync word failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_SYNC_WORD_1ACFFC1D, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_SYNC_WORD, 0x1ACFFC1DUL, "OSync 1ACF", "OOK sync word failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_THRESH_FIXED, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_THRESHOLD_TYPE, (uint32_t)RADIO_MAIN_OOK_THRESHOLD_FIXED, "Thr fixed", "Thr type failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_THRESH_PEAK, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_THRESHOLD_TYPE, (uint32_t)RADIO_MAIN_OOK_THRESHOLD_PEAK, "Thr peak", "Thr type failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_THRESH_AVG, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_THRESHOLD_TYPE, (uint32_t)RADIO_MAIN_OOK_THRESHOLD_AVERAGE, "Thr avg", "Thr type failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_THRESH_4, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_THRESHOLD_VALUE, 4UL, "Thr=4", "Thr value failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_THRESH_8, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_THRESHOLD_VALUE, 8UL, "Thr=8", "Thr value failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_THRESH_12, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_THRESHOLD_VALUE, 12UL, "Thr=12", "Thr value failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_THRESH_16, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_THRESHOLD_VALUE, 16UL, "Thr=16", "Thr value failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_THRESH_24, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_THRESHOLD_VALUE, 24UL, "Thr=24", "Thr value failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_THRESH_32, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_THRESHOLD_VALUE, 32UL, "Thr=32", "Thr value failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_THRESH_48, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_THRESHOLD_VALUE, 48UL, "Thr=48", "Thr value failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_THRESH_64, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_THRESHOLD_VALUE, 64UL, "Thr=64", "Thr value failed"),
    MENU_RADIO_SET(MENU_ACTION_MOD_OOK_RESET, RADIO_MAIN_MODULATION_OOK, RADIO_MAIN_OPTION_OOK_RESET_DEFAULTS, 0UL, "OOK reset", "OOK reset failed")
};

#undef MENU_RADIO_ACTIVATE
#undef MENU_RADIO_SET
#undef MENU_RADIO_PRESET

static const osThreadAttr_t s_menu_task_attr =
{
    .name = "menu_task",
    .priority = (osPriority_t)osPriorityLow,
    .stack_mem = s_menu_task_stack,
    .stack_size = sizeof(s_menu_task_stack),
    .cb_mem = &s_menu_task_cb,
    .cb_size = sizeof(s_menu_task_cb)
};

void menu_main_create_task(void)
{
    if (s_menu_notify_queue == NULL)
    {
        s_menu_notify_queue = osMessageQueueNew(MENU_NOTIFY_QUEUE_DEPTH, sizeof(menu_notification_t), NULL);
        if (s_menu_notify_queue == NULL)
        {
            return;
        }
    }

    if (s_menu_task == NULL)
    {
        s_menu_task = osThreadNew(menu_main_task_fn, NULL, &s_menu_task_attr);
    }
}

bool menu_main_post_notification(const menu_notification_t *n)
{
    menu_notification_t dropped;

    if ((n == NULL) || (s_menu_notify_queue == NULL))
    {
        return false;
    }

    if (osMessageQueuePut(s_menu_notify_queue, n, 0U, 0U) == osOK)
    {
        return true;
    }

    if (osMessageQueueGet(s_menu_notify_queue, &dropped, NULL, 0U) == osOK)
    {
        return (osMessageQueuePut(s_menu_notify_queue, n, 0U, 0U) == osOK);
    }

    return false;
}

static void menu_main_task_fn(void *argument)
{
    menu_state_t st;
    button_event_t evt;
    menu_notification_t notify;
    security_runtime_cfg_t cfg;

    (void)argument;
    memset(&st, 0, sizeof(st));
    st.popup_enabled = true;
    st.selected_device_slot = MENU_DEVICE_SLOT_INVALID;
    st.last_input_ms = HAL_GetTick();

    if (security_main_cmd_get_runtime_cfg(&cfg))
    {
        st.popup_enabled = (cfg.notify_mode == SECURITY_NOTIFY_POPUP);
    }

    menu_enter_monitor(&st);

    for (;;)
    {
        if (button_main_get_event(&evt, 25U))
        {
            st.last_input_ms = HAL_GetTick();
            menu_handle_button(&st, evt);
        }

        while ((s_menu_notify_queue != NULL) &&
               (osMessageQueueGet(s_menu_notify_queue, &notify, NULL, 0U) == osOK))
        {
            menu_handle_notification(&st, &notify);
        }

        if (st.modal == MENU_MODAL_QUICK_REPLY)
        {
            uint32_t now_ms = HAL_GetTick();
            uint32_t remaining_ms = (st.quick_reply_deadline_ms > now_ms) ?
                                    (st.quick_reply_deadline_ms - now_ms) : 0U;
            uint32_t remaining_seconds = (remaining_ms + 999U) / 1000U;

            if (remaining_ms == 0U)
            {
                menu_show_action_result(&st, MENU_NOTIFICATION_WARNING, "Reply timeout");
            }
            else if (remaining_seconds != st.quick_reply_last_seconds)
            {
                st.quick_reply_last_seconds = remaining_seconds;
                menu_render_quick_reply(&st);
            }
        }

        if ((st.current_page != MENU_PAGE_NONE) &&
            (st.modal == MENU_MODAL_NONE) &&
            ((HAL_GetTick() - st.last_input_ms) >= MENU_INACTIVITY_TIMEOUT_MS))
        {
            menu_enter_monitor(&st);
        }
    }
}

/* Returns the UI to passive monitor mode and clears transient menu state. */
static void menu_enter_monitor(menu_state_t *st)
{
    if (st == NULL)
    {
        return;
    }

    st->current_page = MENU_PAGE_NONE;
    st->selected_idx = 0U;
    st->modal = MENU_MODAL_NONE;
    st->pending_action = MENU_ACTION_NONE;
    st->selected_device_slot = MENU_DEVICE_SLOT_INVALID;
    st->pending_target_node_id = 0U;
    st->send_target_action = MENU_ACTION_NONE;
    st->transient_parent_page = MENU_PAGE_NONE;
    st->pairing_network_mode = false;
    st->quick_reply_src_id = 0U;
    st->quick_reply_deadline_ms = 0U;
    st->quick_reply_last_seconds = 0U;
    st->user_unlock_until_ms = 0UL;
    st->admin_unlock_until_ms = 0UL;
    menu_clear_tx_ack_state(st);
    menu_clear_pin_state(st);
    menu_line_clear(st->quick_reply_text);
    (void)lcd_main_set_mode(LCD_MODE_MONITOR);
}

/* Opens a normal page and resets selection/modal context. */
static void menu_open_page(menu_state_t *st, menu_page_id_t page_id)
{
    const menu_page_t *page;

    if (st == NULL)
    {
        return;
    }

    st->current_page = page_id;
    st->selected_idx = 0U;
    st->modal = MENU_MODAL_NONE;
    st->pending_action = MENU_ACTION_NONE;
    st->pending_target_node_id = 0U;
    st->send_target_action = MENU_ACTION_NONE;
    st->transient_parent_page = MENU_PAGE_NONE;
    st->pairing_network_mode = false;
    st->quick_reply_src_id = 0U;
    st->quick_reply_deadline_ms = 0U;
    st->quick_reply_last_seconds = 0U;
    menu_clear_tx_ack_state(st);
    menu_line_clear(st->quick_reply_text);
    page = menu_get_page(page_id);
    if (page != NULL)
    {
        while ((st->selected_idx < page->item_count) &&
               !menu_item_is_selectable(st, page, st->selected_idx))
        {
            st->selected_idx++;
        }
        if ((page->item_count > 0U) && (st->selected_idx >= page->item_count))
        {
            st->selected_idx = 0U;
        }
    }
    menu_render(st);
}

/* Shows an informational modal without changing the underlying page. */
static void menu_open_info_modal(menu_state_t *st,
                                 const char *l0,
                                 const char *l1,
                                 const char *l2,
                                 const char *l3)
{
    if (st == NULL)
    {
        return;
    }

    menu_clear_tx_ack_state(st);
    st->modal = MENU_MODAL_INFO;
    st->pending_action = MENU_ACTION_NONE;
    menu_render_popup(l0, l1, l2, l3);
}

static void menu_clear_tx_ack_state(menu_state_t *st)
{
    if (st == NULL)
    {
        return;
    }

    st->tx_ack_waiting = false;
    menu_line_clear(st->tx_sent_text);
}

/* Closes the active modal and redraws the underlying page or monitor. */
static void menu_close_modal(menu_state_t *st)
{
    if (st == NULL)
    {
        return;
    }

    if (st->modal == MENU_MODAL_PIN)
    {
        menu_clear_pin_state(st);
    }
    if (st->modal == MENU_MODAL_INFO)
    {
        menu_clear_tx_ack_state(st);
    }
    st->modal = MENU_MODAL_NONE;
    st->pending_action = MENU_ACTION_NONE;

    if (st->current_page == MENU_PAGE_NONE)
    {
        (void)lcd_main_set_mode(LCD_MODE_MONITOR);
    }
    else
    {
        menu_render(st);
    }
}

static void menu_line_clear(char *dst)
{
    uint8_t i;

    if (dst == NULL)
    {
        return;
    }

    for (i = 0U; i < MENU_LINE_CHARS; i++)
    {
        dst[i] = ' ';
    }
    dst[MENU_LINE_CHARS] = '\0';
}

static uint8_t menu_line_copy(char *dst, uint8_t offset, const char *src, uint8_t max_chars)
{
    uint8_t copied = 0U;

    if ((dst == NULL) || (src == NULL) || (offset >= MENU_LINE_CHARS))
    {
        return offset;
    }

    while ((offset < MENU_LINE_CHARS) &&
           (copied < max_chars) &&
           (src[copied] != '\0'))
    {
        dst[offset++] = src[copied++];
    }

    return offset;
}

static uint8_t menu_line_append_u32(char *dst, uint8_t offset, uint32_t value)
{
    char digits[10];
    uint8_t count = 0U;

    if ((dst == NULL) || (offset >= MENU_LINE_CHARS))
    {
        return offset;
    }

    do
    {
        digits[count++] = (char)('0' + (value % 10UL));
        value /= 10UL;
    } while ((value != 0UL) && (count < sizeof(digits)));

    while ((count > 0U) && (offset < MENU_LINE_CHARS))
    {
        dst[offset++] = digits[--count];
    }

    return offset;
}

static uint8_t menu_line_append_i32(char *dst, uint8_t offset, int32_t value)
{
    uint32_t magnitude;

    if ((dst == NULL) || (offset >= MENU_LINE_CHARS))
    {
        return offset;
    }

    if (value < 0)
    {
        dst[offset++] = '-';
        magnitude = (uint32_t)(-(int64_t)value);
    }
    else
    {
        magnitude = (uint32_t)value;
    }

    return menu_line_append_u32(dst, offset, magnitude);
}

static uint8_t menu_line_append_hex32(char *dst, uint8_t offset, uint32_t value)
{
    static const char hex[] = "0123456789ABCDEF";
    int8_t nibble;

    if ((dst == NULL) || (offset >= MENU_LINE_CHARS))
    {
        return offset;
    }

    for (nibble = 7; (nibble >= 0) && (offset < MENU_LINE_CHARS); nibble--)
    {
        dst[offset++] = hex[(value >> ((uint32_t)nibble * 4UL)) & 0x0FU];
    }

    return offset;
}

static uint8_t menu_line_append_freq_mhz(char *dst, uint8_t offset, uint32_t freq_hz)
{
    uint32_t mhz;
    uint32_t frac_hz;
    char frac_digits[6];
    uint8_t frac_len = 6U;
    int8_t idx;

    if ((dst == NULL) || (offset >= MENU_LINE_CHARS))
    {
        return offset;
    }

    mhz = freq_hz / 1000000UL;
    frac_hz = freq_hz % 1000000UL;
    offset = menu_line_append_u32(dst, offset, mhz);

    if (frac_hz == 0UL)
    {
        return offset;
    }

    if (offset < MENU_LINE_CHARS)
    {
        dst[offset++] = '.';
    }

    for (idx = 5; idx >= 0; idx--)
    {
        frac_digits[idx] = (char)('0' + (frac_hz % 10UL));
        frac_hz /= 10UL;
    }

    while ((frac_len > 1U) && (frac_digits[frac_len - 1U] == '0'))
    {
        frac_len--;
    }

    for (idx = 0; (idx < (int8_t)frac_len) && (offset < MENU_LINE_CHARS); idx++)
    {
        dst[offset++] = frac_digits[idx];
    }

    return offset;
}

static void menu_line_format_u32(char *dst, const char *prefix, uint32_t value, const char *suffix)
{
    uint8_t offset = 0U;

    menu_line_clear(dst);
    offset = menu_line_copy(dst, offset, prefix, MENU_LINE_CHARS);
    offset = menu_line_append_u32(dst, offset, value);
    (void)menu_line_copy(dst, offset, suffix, MENU_LINE_CHARS);
}

static void menu_line_format_i32(char *dst, const char *prefix, int32_t value, const char *suffix)
{
    uint8_t offset = 0U;

    menu_line_clear(dst);
    offset = menu_line_copy(dst, offset, prefix, MENU_LINE_CHARS);
    offset = menu_line_append_i32(dst, offset, value);
    (void)menu_line_copy(dst, offset, suffix, MENU_LINE_CHARS);
}

static void menu_line_format_hex32(char *dst, const char *prefix, uint32_t value)
{
    uint8_t offset = 0U;

    menu_line_clear(dst);
    offset = menu_line_copy(dst, offset, prefix, MENU_LINE_CHARS);
    offset = menu_line_copy(dst, offset, "0x", 2U);
    (void)menu_line_append_hex32(dst, offset, value);
}

static void menu_line_format_device_ref(char *dst, const char *prefix, uint32_t device_code)
{
    uint8_t offset = 0U;

    menu_line_clear(dst);
    offset = menu_line_copy(dst, offset, prefix, MENU_LINE_CHARS);
    if (device_code == LAVIET_GATEWAY_ID)
    {
        (void)menu_line_copy(dst, offset, "gateway", MENU_LINE_CHARS);
        return;
    }
    if (device_code == LAVIET_BROADCAST_ID)
    {
        (void)menu_line_copy(dst, offset, "broadcast", MENU_LINE_CHARS);
        return;
    }

    offset = menu_line_copy(dst, offset, "0x", 2U);
    (void)menu_line_append_hex32(dst, offset, device_code);
}

static void menu_line_format_source(char *dst, uint32_t device_code)
{
    menu_line_format_device_ref(dst, "From ", device_code);
}

static void menu_line_format_freq(char *dst, const char *prefix, uint32_t freq_hz)
{
    uint8_t offset = 0U;

    menu_line_clear(dst);
    offset = menu_line_copy(dst, offset, prefix, MENU_LINE_CHARS);
    offset = menu_line_append_freq_mhz(dst, offset, freq_hz);
    (void)menu_line_copy(dst, offset, " MHz", 4U);
}

static void menu_line_format_fixed2(char *dst, const char *prefix, float value, const char *suffix)
{
    int32_t scaled;
    uint32_t magnitude;
    uint8_t offset = 0U;

    menu_line_clear(dst);
    offset = menu_line_copy(dst, offset, prefix, MENU_LINE_CHARS);

    if (value >= 0.0f)
    {
        scaled = (int32_t)((value * 100.0f) + 0.5f);
    }
    else
    {
        scaled = (int32_t)((value * 100.0f) - 0.5f);
    }

    if (scaled < 0)
    {
        magnitude = (uint32_t)(-(int64_t)scaled);
    }
    else
    {
        magnitude = (uint32_t)scaled;
    }

    if (scaled < 0)
    {
        offset = menu_line_copy(dst, offset, "-", 1U);
    }

    offset = menu_line_append_u32(dst, offset, magnitude / 100UL);
    offset = menu_line_copy(dst, offset, ".", 1U);
    if ((magnitude % 100UL) < 10UL)
    {
        offset = menu_line_copy(dst, offset, "0", 1U);
    }
    offset = menu_line_append_u32(dst, offset, magnitude % 100UL);
    (void)menu_line_copy(dst, offset, suffix, MENU_LINE_CHARS);
}

static void menu_line_copy_or_default(char *dst, const char *text, const char *fallback)
{
    if (dst == NULL)
    {
        return;
    }

    menu_line_clear(dst);

    if (text != NULL)
    {
        (void)menu_line_copy(dst, 0U, text, MENU_LINE_CHARS);
        return;
    }

    if (fallback != NULL)
    {
        (void)menu_line_copy(dst, 0U, fallback, MENU_LINE_CHARS);
    }
}

static const char *menu_pair_code_text(const char *text, uint8_t prefix_len)
{
    if ((text != NULL) && (text[prefix_len] == ' '))
    {
        return &text[prefix_len + 1U];
    }

    return "----";
}

static bool menu_get_trusted_slot(uint8_t slot, trusted_info_t *info_out)
{
    if (info_out == NULL)
    {
        return false;
    }

    memset(info_out, 0, sizeof(*info_out));
    return (slot < MENU_TRUSTED_DEVICE_SLOTS) &&
           security_main_cmd_get_device(slot, info_out) &&
           info_out->in_use;
}

static bool menu_gateway_slot_in_use(void)
{
    trusted_info_t info;

    return menu_get_trusted_slot(0U, &info) &&
           (info.node_id == LAVIET_GATEWAY_ID);
}

static uint8_t menu_line_append_device_slot_name(char *dst, uint8_t offset, uint8_t slot)
{
    if ((dst == NULL) || (offset >= MENU_LINE_CHARS))
    {
        return offset;
    }

    if (slot == 0U)
    {
        return menu_line_copy(dst, offset, "G", 1U);
    }

    offset = menu_line_copy(dst, offset, "D", 1U);
    return menu_line_append_u32(dst, offset, slot);
}

static void menu_build_trusted_slot_label(uint8_t slot, const trusted_info_t *info, char *dst)
{
    uint8_t offset = 0U;

    if ((info == NULL) || (dst == NULL))
    {
        return;
    }

    menu_line_clear(dst);
    offset = menu_line_append_device_slot_name(dst, offset, slot);
    offset = menu_line_copy(dst, offset, " ", 1U);
    if (info->node_id == LAVIET_GATEWAY_ID)
    {
        (void)menu_line_copy(dst, offset, "gateway", 7U);
    }
    else if (info->node_id == LAVIET_BROADCAST_ID)
    {
        (void)menu_line_copy(dst, offset, "broadcast", 9U);
    }
    else
    {
        offset = menu_line_copy(dst, offset, "0x", 2U);
        (void)menu_line_append_hex32(dst, offset, info->node_id);
    }
}

static void menu_build_empty_trusted_slot_label(uint8_t slot, char *dst)
{
    uint8_t offset = 0U;

    if (dst == NULL)
    {
        return;
    }

    menu_line_clear(dst);
    offset = menu_line_append_device_slot_name(dst, offset, slot);
    (void)menu_line_copy(dst, offset, " (empty)", 8U);
}

static const char *menu_lora_bw_text(radio_lora_bw_t bw)
{
    if (bw == RADIO_LORA_BW_7_8_KHZ)
    {
        return "7.8";
    }
    if (bw == RADIO_LORA_BW_10_4_KHZ)
    {
        return "10.4";
    }
    if (bw == RADIO_LORA_BW_15_6_KHZ)
    {
        return "15.6";
    }
    if (bw == RADIO_LORA_BW_20_8_KHZ)
    {
        return "20.8";
    }
    if (bw == RADIO_LORA_BW_31_25_KHZ)
    {
        return "31.25";
    }
    if (bw == RADIO_LORA_BW_41_7_KHZ)
    {
        return "41.7";
    }
    if (bw == RADIO_LORA_BW_62_5_KHZ)
    {
        return "62.5";
    }
    if (bw == RADIO_LORA_BW_125_KHZ)
    {
        return "125";
    }
    if (bw == RADIO_LORA_BW_250_KHZ)
    {
        return "250";
    }
    if (bw == RADIO_LORA_BW_500_KHZ)
    {
        return "500";
    }

    return "?";
}

static const char *menu_lora_cr_text(uint8_t denominator)
{
    if (denominator == 5U)
    {
        return "4/5";
    }
    if (denominator == 6U)
    {
        return "4/6";
    }
    if (denominator == 7U)
    {
        return "4/7";
    }
    if (denominator == 8U)
    {
        return "4/8";
    }

    return "?";
}

static const char *menu_fsk_shape_text(radio_main_fsk_shaping_t shaping)
{
    if (shaping == RADIO_MAIN_FSK_SHAPING_GFSK)
    {
        return "GFSK";
    }
    if (shaping == RADIO_MAIN_FSK_SHAPING_MSK)
    {
        return "MSK";
    }
    if (shaping == RADIO_MAIN_FSK_SHAPING_GMSK)
    {
        return "GMSK";
    }

    return "FSK";
}

static const char *menu_filter_text(radio_main_filter_t filter)
{
    if (filter == RADIO_MAIN_FILTER_BT_10)
    {
        return "BT1.0";
    }
    if (filter == RADIO_MAIN_FILTER_BT_07)
    {
        return "BT0.7";
    }
    if (filter == RADIO_MAIN_FILTER_BT_05)
    {
        return "BT0.5";
    }
    if (filter == RADIO_MAIN_FILTER_BT_03)
    {
        return "BT0.3";
    }

    return "OFF";
}

static const char *menu_crc_text(radio_main_crc_type_t crc_type)
{
    if (crc_type == RADIO_MAIN_CRC_SX1276)
    {
        return "SX";
    }
    if (crc_type == RADIO_MAIN_CRC_IBM)
    {
        return "IBM";
    }
    if (crc_type == RADIO_MAIN_CRC_CCITT)
    {
        return "CCITT";
    }

    return "OFF";
}

static const char *menu_ook_threshold_text(radio_main_ook_threshold_t threshold)
{
    if (threshold == RADIO_MAIN_OOK_THRESHOLD_PEAK)
    {
        return "Peak";
    }
    if (threshold == RADIO_MAIN_OOK_THRESHOLD_AVERAGE)
    {
        return "Avg";
    }

    return "Fix";
}

static const char *menu_address_filter_text(radio_main_address_filter_t filter)
{
    if (filter == RADIO_MAIN_ADDRESS_FILTER_NODE)
    {
        return "NODE";
    }
    if (filter == RADIO_MAIN_ADDRESS_FILTER_NODE_BROADCAST)
    {
        return "NODE+BC";
    }

    return "OFF";
}

static uint8_t menu_radio_settings_item_count(const radio_main_runtime_cfg_t *cfg)
{
    if (cfg == NULL)
    {
        return 1U;
    }

    if (cfg->active_modulation == RADIO_MAIN_MODULATION_FSK)
    {
        return 14U;
    }

    if (cfg->active_modulation == RADIO_MAIN_MODULATION_OOK)
    {
        return 12U;
    }

    return 13U;
}

static void menu_build_radio_settings_item(const radio_main_runtime_cfg_t *cfg,
                                           uint8_t item_idx,
                                           char *dst)
{
    uint8_t offset = 0U;

    menu_line_clear(dst);
    if ((cfg == NULL) || (dst == NULL))
    {
        return;
    }

    if (cfg->active_modulation == RADIO_MAIN_MODULATION_FSK)
    {
        if (item_idx == 0U)
        {
            (void)menu_line_copy(dst, 0U, "Mode FSK/GxSK", MENU_LINE_CHARS);
        }
        else if (item_idx == 1U)
        {
            menu_line_format_freq(dst, "Freq ", cfg->fsk.frequency_hz);
        }
        else if (item_idx == 2U)
        {
            menu_line_format_i32(dst, "Power ", cfg->fsk.tx_power_dbm, " dBm");
        }
        else if (item_idx == 3U)
        {
            offset = menu_line_copy(dst, 0U, "Shape ", MENU_LINE_CHARS);
            (void)menu_line_copy(dst, offset, menu_fsk_shape_text(cfg->fsk.shaping), 10U);
        }
        else if (item_idx == 4U)
        {
            menu_line_format_u32(dst, "Bitrate ", cfg->fsk.bitrate_bps, " bps");
        }
        else if (item_idx == 5U)
        {
            offset = menu_line_copy(dst, 0U, "RX BW ", MENU_LINE_CHARS);
            offset = menu_line_copy(dst, offset, menu_lora_bw_text(cfg->fsk.rx_bandwidth), 8U);
            (void)menu_line_copy(dst, offset, " kHz", 4U);
        }
        else if (item_idx == 6U)
        {
            offset = menu_line_copy(dst, 0U, "Filter ", MENU_LINE_CHARS);
            (void)menu_line_copy(dst, offset, menu_filter_text(cfg->fsk.filter), 8U);
        }
        else if (item_idx == 7U)
        {
            menu_line_format_u32(dst, "Preamble ", cfg->fsk.preamble_len, " B");
        }
        else if (item_idx == 8U)
        {
            menu_line_format_u32(dst, "Sync len ", cfg->fsk.sync_word_len, " B");
        }
        else if (item_idx == 9U)
        {
            menu_line_format_hex32(dst, "Sync ", (uint32_t)cfg->fsk.sync_word);
        }
        else if (item_idx == 10U)
        {
            offset = menu_line_copy(dst, 0U, "Addr ", MENU_LINE_CHARS);
            (void)menu_line_copy(dst, offset, menu_address_filter_text(cfg->fsk.address_filter), 10U);
        }
        else if (item_idx == 11U)
        {
            offset = menu_line_copy(dst, 0U, "CRC ", MENU_LINE_CHARS);
            (void)menu_line_copy(dst, offset, menu_crc_text(cfg->fsk.crc_type), 10U);
        }
        else if (item_idx == 12U)
        {
            offset = menu_line_copy(dst, 0U, "Whitening ", MENU_LINE_CHARS);
            (void)menu_line_copy(dst, offset, cfg->fsk.data_whitening ? "ON" : "OFF", 3U);
        }
        return;
    }

    if (cfg->active_modulation == RADIO_MAIN_MODULATION_OOK)
    {
        if (item_idx == 0U)
        {
            (void)menu_line_copy(dst, 0U, "Mode OOK", MENU_LINE_CHARS);
        }
        else if (item_idx == 1U)
        {
            menu_line_format_freq(dst, "Freq ", cfg->ook.frequency_hz);
        }
        else if (item_idx == 2U)
        {
            menu_line_format_i32(dst, "Power ", cfg->ook.tx_power_dbm, " dBm");
        }
        else if (item_idx == 3U)
        {
            menu_line_format_u32(dst, "Bitrate ", cfg->ook.bitrate_bps, " bps");
        }
        else if (item_idx == 4U)
        {
            offset = menu_line_copy(dst, 0U, "RX BW ", MENU_LINE_CHARS);
            offset = menu_line_copy(dst, offset, menu_lora_bw_text(cfg->ook.rx_bandwidth), 8U);
            (void)menu_line_copy(dst, offset, " kHz", 4U);
        }
        else if (item_idx == 5U)
        {
            menu_line_format_u32(dst, "Preamble ", cfg->ook.preamble_len, " B");
        }
        else if (item_idx == 6U)
        {
            menu_line_format_u32(dst, "Sync len ", cfg->ook.sync_word_len, " B");
        }
        else if (item_idx == 7U)
        {
            menu_line_format_hex32(dst, "Sync ", cfg->ook.sync_word);
        }
        else if (item_idx == 8U)
        {
            offset = menu_line_copy(dst, 0U, "Thresh ", MENU_LINE_CHARS);
            (void)menu_line_copy(dst, offset, menu_ook_threshold_text(cfg->ook.threshold), 8U);
        }
        else if (item_idx == 9U)
        {
            menu_line_format_u32(dst, "Thr value ", cfg->ook.threshold_value, "");
        }
        else if (item_idx == 10U)
        {
            offset = menu_line_copy(dst, 0U, "Coding ", MENU_LINE_CHARS);
            (void)menu_line_copy(dst, offset, cfg->coding_enabled ? "ON" : "OFF", 3U);
        }
        return;
    }

    if (item_idx == 0U)
    {
        (void)menu_line_copy(dst, 0U, "Mode LoRa", MENU_LINE_CHARS);
    }
    else if (item_idx == 1U)
    {
        menu_line_format_freq(dst, "Freq ", cfg->lora.frequency_hz);
    }
    else if (item_idx == 2U)
    {
        menu_line_format_i32(dst, "Power ", cfg->lora.tx_power_dbm, " dBm");
    }
    else if (item_idx == 3U)
    {
        offset = menu_line_copy(dst, 0U, "BW ", MENU_LINE_CHARS);
        offset = menu_line_copy(dst, offset, menu_lora_bw_text(cfg->lora.bandwidth), 8U);
        (void)menu_line_copy(dst, offset, " kHz", 4U);
    }
    else if (item_idx == 4U)
    {
        menu_line_format_u32(dst, "SF ", cfg->lora.spreading_factor, "");
    }
    else if (item_idx == 5U)
    {
        offset = menu_line_copy(dst, 0U, "CR ", MENU_LINE_CHARS);
        (void)menu_line_copy(dst, offset, menu_lora_cr_text(cfg->lora.coding_rate), 6U);
    }
    else if (item_idx == 6U)
    {
        offset = menu_line_copy(dst, 0U, "CRC ", MENU_LINE_CHARS);
        (void)menu_line_copy(dst, offset, cfg->lora.crc_on ? "ON" : "OFF", 3U);
    }
    else if (item_idx == 7U)
    {
        menu_line_format_u32(dst, "Preamble ", cfg->lora.preamble_len, " sym");
    }
    else if (item_idx == 8U)
    {
        offset = menu_line_copy(dst, 0U, "Header ", MENU_LINE_CHARS);
        (void)menu_line_copy(dst, offset,
                             cfg->lora.implicit_header ? "Implicit" : "Explicit",
                             8U);
    }
    else if (item_idx == 9U)
    {
        offset = menu_line_copy(dst, 0U, "I/Q ", MENU_LINE_CHARS);
        (void)menu_line_copy(dst, offset, cfg->lora.invert_iq ? "Invert" : "Normal", 6U);
    }
    else if (item_idx == 10U)
    {
        menu_line_format_hex32(dst, "Sync ", cfg->lora.sync_word);
    }
    else if (item_idx == 11U)
    {
        offset = menu_line_copy(dst, 0U, "Code ", MENU_LINE_CHARS);
        offset = menu_line_copy(dst, offset, cfg->coding_enabled ? "ON" : "OFF", 3U);
        offset = menu_line_copy(dst, offset, " FH ", 4U);
        (void)menu_line_copy(dst, offset, cfg->fh_enabled ? "ON" : "OFF", 3U);
    }
}

static void menu_build_page_title(const menu_state_t *st, const menu_page_t *page, char *dst)
{
    menu_line_clear(dst);
    if ((st == NULL) || (page == NULL) || (dst == NULL))
    {
        return;
    }

    (void)menu_line_copy(dst, 0U, page->title, MENU_LINE_CHARS);
}

static void menu_build_item_label(const menu_state_t *st,
                                  const menu_page_t *page,
                                  uint8_t item_idx,
                                  char *dst)
{
    radio_main_runtime_cfg_t radio_cfg;
    trusted_info_t info;
    uint32_t period_ms;
    uint8_t offset = 0U;

    menu_line_clear(dst);
    if ((st == NULL) || (page == NULL) || (dst == NULL))
    {
        return;
    }

    if ((st->current_page == MENU_PAGE_DEVICE_DELETE_LIST) &&
        (item_idx < MENU_TRUSTED_DEVICE_SLOTS))
    {
        memset(&info, 0, sizeof(info));
        if (menu_get_trusted_slot(item_idx, &info))
        {
            menu_build_trusted_slot_label(item_idx, &info, dst);
        }
        else
        {
            menu_build_empty_trusted_slot_label(item_idx, dst);
        }
        return;
    }

    if ((st->current_page == MENU_PAGE_SEND_DIRECT_LIST) &&
        (item_idx < MENU_TRUSTED_DEVICE_SLOTS))
    {
        memset(&info, 0, sizeof(info));
        if (menu_get_trusted_slot(item_idx, &info))
        {
            menu_build_trusted_slot_label(item_idx, &info, dst);
        }
        else
        {
            menu_build_empty_trusted_slot_label(item_idx, dst);
        }
        return;
    }

    if ((st->current_page == MENU_PAGE_SEND_TARGET_LIST) &&
        (item_idx < MENU_TRUSTED_DEVICE_SLOTS))
    {
        memset(&info, 0, sizeof(info));
        if (menu_get_trusted_slot(item_idx, &info))
        {
            menu_build_trusted_slot_label(item_idx, &info, dst);
        }
        else
        {
            menu_build_empty_trusted_slot_label(item_idx, dst);
        }
        return;
    }

    if ((st->current_page == MENU_PAGE_PIN_SETTINGS) && (item_idx == 0U))
    {
        (void)menu_line_copy(dst, 0U, s_pin_enabled ? "PIN ON" : "PIN OFF", MENU_LINE_CHARS);
        return;
    }

    if ((st->current_page == MENU_PAGE_RADIO_SETTINGS) &&
        radio_main_get_runtime_cfg(&radio_cfg))
    {
        uint8_t item_count = menu_radio_settings_item_count(&radio_cfg);

        if (item_idx < item_count)
        {
            if (item_idx == (uint8_t)(item_count - 1U))
            {
                (void)menu_line_copy(dst, 0U, "Back", MENU_LINE_CHARS);
            }
            else
            {
                menu_build_radio_settings_item(&radio_cfg, item_idx, dst);
            }
        }
        return;
    }

    if ((st->current_page == MENU_PAGE_DEVICE_DELETE_ACTION) &&
        (item_idx == 0U))
    {
        memset(&info, 0, sizeof(info));
        if ((st->selected_device_slot != MENU_DEVICE_SLOT_INVALID) &&
            menu_get_trusted_slot(st->selected_device_slot, &info))
        {
            menu_build_trusted_slot_label(st->selected_device_slot, &info, dst);
        }
        else
        {
            (void)menu_line_copy(dst, offset, "No device selected", 18U);
        }
        return;
    }

    if ((st->current_page == MENU_PAGE_SECURITY_AUTOPING) &&
        (item_idx == 0U))
    {
        if (radio_main_get_auto_ping_period_ms(&period_ms))
        {
            offset = menu_line_copy(dst, offset, "Period ", 7U);
            offset = menu_line_append_u32(dst, offset, period_ms);
            (void)menu_line_copy(dst, offset, " ms", 3U);
        }
        else
        {
            (void)menu_line_copy(dst, offset, "Period unavailable", 18U);
        }
        return;
    }

    if ((item_idx < page->item_count) && (page->items[item_idx].label != NULL))
    {
        (void)menu_line_copy(dst, 0U, page->items[item_idx].label, MENU_LINE_CHARS);
    }
}

static void menu_render(menu_state_t *st)
{
    char screen[MENU_DISPLAY_ROWS][MENU_LINE_BUF_SIZE];
    char label[MENU_LINE_BUF_SIZE];
    const menu_page_t *page;
    uint8_t row;
    uint8_t start_idx;

    if (st == NULL)
    {
        return;
    }

    page = menu_get_page(st->current_page);
    if (page == NULL)
    {
        return;
    }

    for (row = 0U; row < MENU_DISPLAY_ROWS; row++)
    {
        menu_line_clear(screen[row]);
    }

    menu_build_page_title(st, page, screen[0]);

    start_idx = (uint8_t)((st->selected_idx / MENU_ITEMS_VISIBLE) * MENU_ITEMS_VISIBLE);
    for (row = 0U; row < MENU_ITEMS_VISIBLE; row++)
    {
        uint8_t item_idx = (uint8_t)(start_idx + row);
        uint8_t dst_row = (uint8_t)(row + 1U);

        if (item_idx < page->item_count)
        {
            screen[dst_row][0] = ' ';
            if (item_idx == st->selected_idx)
            {
                screen[dst_row][0] = '>';
            }

            menu_build_item_label(st, page, item_idx, label);

            (void)menu_line_copy(screen[dst_row],
                                 1U,
                                 label,
                                 (uint8_t)(MENU_LINE_CHARS - 1U));
        }
    }

    (void)lcd_main_show_menu(screen[0], screen[1], screen[2], screen[3]);
}

static void menu_render_popup(const char *l0, const char *l1, const char *l2, const char *l3)
{
    char line0[MENU_LINE_BUF_SIZE];
    char line1[MENU_LINE_BUF_SIZE];
    char line2[MENU_LINE_BUF_SIZE];
    char line3[MENU_LINE_BUF_SIZE];

    menu_line_copy_or_default(line0, l0, "");
    menu_line_copy_or_default(line1, l1, "");
    menu_line_copy_or_default(line2, l2, "");
    menu_line_copy_or_default(line3, l3, "");

    (void)lcd_main_show_popup(line0, line1, line2, line3);
}

static void menu_render_quick_reply(menu_state_t *st)
{
    char line1[MENU_LINE_BUF_SIZE];
    char line2[MENU_LINE_BUF_SIZE];
    char line3[MENU_LINE_BUF_SIZE];

    if (st == NULL)
    {
        return;
    }

    menu_line_format_source(line1, st->quick_reply_src_id);
    menu_line_copy_or_default(line2, "UP=no OK=ok DN=yes", "");
    menu_line_format_u32(line3, "Reply in ", st->quick_reply_last_seconds, " s");
    menu_render_popup(st->quick_reply_text, line1, line2, line3);
}

static void menu_render_pin(menu_state_t *st)
{
    char line1[MENU_LINE_BUF_SIZE];
    const char *title = "USER PIN";
    uint8_t offset;
    uint8_t i;

    if (st == NULL)
    {
        return;
    }

    if (st->pending_auth_level == MENU_AUTH_ADMIN)
    {
        title = "ADMIN PIN";
    }
    if (st->pending_action == MENU_ACTION_PIN_SET_USER)
    {
        title = "NEW USER PIN";
    }
    else if (st->pending_action == MENU_ACTION_PIN_SET_ADMIN)
    {
        title = "NEW ADMIN PIN";
    }

    menu_line_clear(line1);
    offset = menu_line_copy(line1, 0U, "PIN: ", 5U);
    for (i = 0U; i < MENU_SETTINGS_PIN_LEN; i++)
    {
        char digit = '_';

        if (i < st->pin_index)
        {
            digit = '*';
        }
        else if (i == st->pin_index)
        {
            digit = (char)('0' + st->pin_digits[i]);
        }

        if (offset < MENU_LINE_CHARS)
        {
            line1[offset++] = digit;
        }
        if (offset < MENU_LINE_CHARS)
        {
            line1[offset++] = ' ';
        }
    }

    menu_render_popup(title, line1, "UP/DN digit", "OK=next HOLD=cancel");
}

static menu_auth_level_t menu_page_auth_level(menu_page_id_t page_id)
{
    switch (page_id)
    {
        case MENU_PAGE_PAGER:
        case MENU_PAGE_RADIO_SETTINGS:
        case MENU_PAGE_SEND_OPTIONS:
        case MENU_PAGE_SEND_DIRECT_LIST:
        case MENU_PAGE_SEND_TARGET_LIST:
        case MENU_PAGE_MSG_GROUPS:
        case MENU_PAGE_GROUP_ALERT:
        case MENU_PAGE_GROUP_STATUS:
        case MENU_PAGE_GROUP_SERVICE:
        case MENU_PAGE_GROUP_QUICK:
            return MENU_AUTH_USER;

        case MENU_PAGE_MAIN:
        case MENU_PAGE_DEVICES:
        case MENU_PAGE_DEVICE_DELETE_LIST:
        case MENU_PAGE_DEVICE_DELETE_ACTION:
        case MENU_PAGE_PIN_SETTINGS:
        case MENU_PAGE_SECURITY:
        case MENU_PAGE_SECURITY_FH:
        case MENU_PAGE_SECURITY_CODING:
        case MENU_PAGE_SECURITY_NOTIFY:
        case MENU_PAGE_SECURITY_AUTOPING:
        case MENU_PAGE_HARDWARE:
        case MENU_PAGE_HARDWARE_LED:
        case MENU_PAGE_MODULATION:
        case MENU_PAGE_MOD_LORA:
        case MENU_PAGE_MOD_LORA_FREQ:
        case MENU_PAGE_MOD_LORA_BW:
        case MENU_PAGE_MOD_LORA_SF:
        case MENU_PAGE_MOD_LORA_CR:
        case MENU_PAGE_MOD_LORA_POWER:
        case MENU_PAGE_MOD_LORA_CRC:
        case MENU_PAGE_MOD_LORA_PREAMBLE:
        case MENU_PAGE_MOD_LORA_HEADER:
        case MENU_PAGE_MOD_LORA_IQ:
        case MENU_PAGE_MOD_LORA_SYNC:
        case MENU_PAGE_MOD_FSK:
        case MENU_PAGE_MOD_FSK_SHAPING:
        case MENU_PAGE_MOD_FSK_FREQ:
        case MENU_PAGE_MOD_FSK_BITRATE:
        case MENU_PAGE_MOD_FSK_BW:
        case MENU_PAGE_MOD_FSK_FILTER:
        case MENU_PAGE_MOD_FSK_POWER:
        case MENU_PAGE_MOD_FSK_PREAMBLE:
        case MENU_PAGE_MOD_FSK_SYNC_LEN:
        case MENU_PAGE_MOD_FSK_SYNC_WORD:
        case MENU_PAGE_MOD_FSK_ADDR:
        case MENU_PAGE_MOD_FSK_CRC:
        case MENU_PAGE_MOD_FSK_WHITEN:
        case MENU_PAGE_MOD_OOK:
        case MENU_PAGE_MOD_OOK_FREQ:
        case MENU_PAGE_MOD_OOK_BITRATE:
        case MENU_PAGE_MOD_OOK_POWER:
        case MENU_PAGE_MOD_OOK_BW:
        case MENU_PAGE_MOD_OOK_PREAMBLE:
        case MENU_PAGE_MOD_OOK_SYNC_LEN:
        case MENU_PAGE_MOD_OOK_SYNC_WORD:
        case MENU_PAGE_MOD_OOK_THRESH_TYPE:
        case MENU_PAGE_MOD_OOK_THRESH_VALUE:
        case MENU_PAGE_INFO:
            return MENU_AUTH_ADMIN;

        default:
            return MENU_AUTH_NONE;
    }
}

static bool menu_auth_unlocked(const menu_state_t *st, menu_auth_level_t auth_level)
{
    uint32_t until_ms = 0UL;

    if (!s_pin_enabled)
    {
        return true;
    }

    if ((st == NULL) || (auth_level == MENU_AUTH_NONE))
    {
        return (auth_level == MENU_AUTH_NONE);
    }

    if (auth_level == MENU_AUTH_ADMIN)
    {
        until_ms = st->admin_unlock_until_ms;
    }
    else
    {
        until_ms = st->user_unlock_until_ms;
    }

    if (until_ms == 0UL)
    {
        return false;
    }

    return ((int32_t)(until_ms - HAL_GetTick()) > 0);
}

static bool menu_pin_matches(const menu_state_t *st)
{
    const uint8_t *pin_ref = s_user_pin;
    uint8_t diff = 0U;
    uint8_t i;

    if (st == NULL)
    {
        return false;
    }

    if (st->pending_auth_level == MENU_AUTH_ADMIN)
    {
        pin_ref = s_admin_pin;
    }

    for (i = 0U; i < MENU_SETTINGS_PIN_LEN; i++)
    {
        diff |= (uint8_t)(st->pin_digits[i] ^ pin_ref[i]);
    }

    return (diff == 0U);
}

static bool menu_is_pin_change_action(menu_action_t action)
{
    return ((action == MENU_ACTION_PIN_SET_USER) ||
            (action == MENU_ACTION_PIN_SET_ADMIN));
}

static void menu_clear_pin_state(menu_state_t *st)
{
    if (st == NULL)
    {
        return;
    }

    memset(st->pin_digits, 0, sizeof(st->pin_digits));
    st->pin_index = 0U;
    st->pending_auth_page = MENU_PAGE_NONE;
    st->pending_auth_level = MENU_AUTH_NONE;
}

static void menu_start_pin_change(menu_state_t *st, menu_action_t action)
{
    if ((st == NULL) || !menu_is_pin_change_action(action))
    {
        return;
    }

    st->modal = MENU_MODAL_PIN;
    st->pending_action = action;
    st->pending_auth_page = MENU_PAGE_NONE;
    st->pending_auth_level = MENU_AUTH_NONE;
    memset(st->pin_digits, 0, sizeof(st->pin_digits));
    st->pin_index = 0U;
    menu_render_pin(st);
}

static void menu_request_pin(menu_state_t *st, menu_page_id_t target_page, menu_auth_level_t auth_level)
{
    if (st == NULL)
    {
        return;
    }

    st->modal = MENU_MODAL_PIN;
    st->pending_auth_page = target_page;
    st->pending_auth_level = auth_level;
    memset(st->pin_digits, 0, sizeof(st->pin_digits));
    st->pin_index = 0U;
    menu_render_pin(st);
}

static bool menu_should_open_quick_reply(const char *text)
{
    uint8_t len;
    char last;

    if (text == NULL)
    {
        return false;
    }

    len = 0U;
    while ((len < MENU_LINE_CHARS) && (text[len] != '\0'))
    {
        len++;
    }

    while ((len > 0U) && (text[len - 1U] == ' '))
    {
        len--;
    }

    if (len == 0U)
    {
        return false;
    }

    last = text[len - 1U];
    return ((last == '.') || (last == '?') || (last == '!'));
}

static bool menu_modal_is_preemptible(menu_modal_t modal)
{
    if (modal == MENU_MODAL_INFO)
    {
        return true;
    }

    if (modal == MENU_MODAL_QUICK_REPLY)
    {
        return true;
    }

    return false;
}

static void menu_show_action_result(menu_state_t *st, menu_notification_type_t type, const char *text)
{
    const char *title = "NOTICE";
    char body[MENU_LINE_BUF_SIZE];

    if (type == MENU_NOTIFICATION_WARNING)
    {
        title = "WARNING";
    }
    else if (type == MENU_NOTIFICATION_ERROR)
    {
        title = "ERROR";
    }
    else if (type == MENU_NOTIFICATION_SECURITY)
    {
        title = "SECURITY";
    }
    else if (type == MENU_NOTIFICATION_DELIVERY)
    {
        title = "DELIVERED";
    }
    else if (type == MENU_NOTIFICATION_PAIRING)
    {
        title = "PAIRING";
    }

    menu_line_copy_or_default(body, text, "(empty)");
    menu_open_info_modal(st, title, body, "Any key=back", "");
}

static void menu_show_ok_or_error(menu_state_t *st, bool ok, const char *ok_text, const char *err_text)
{
    char selected[MENU_LINE_BUF_SIZE];

    if (ok)
    {
        menu_line_copy_or_default(selected, ok_text, "");
        menu_show_action_result(st, MENU_NOTIFICATION_SECURITY, selected);
        return;
    }

    menu_line_copy_or_default(selected, err_text, "");
    menu_show_action_result(st, MENU_NOTIFICATION_ERROR, selected);
}

static void menu_show_send_result(menu_state_t *st, bool ok, const char *sent_text)
{
    char line0[MENU_LINE_BUF_SIZE];
    char line1[MENU_LINE_BUF_SIZE];
    char radio_error[MENU_LINE_BUF_SIZE];

    if (!ok)
    {
        menu_line_clear(radio_error);
        if (radio_main_get_last_error_text(radio_error, (uint8_t)sizeof(radio_error)) &&
            (radio_error[0] != '\0'))
        {
            menu_show_action_result(st, MENU_NOTIFICATION_ERROR, radio_error);
        }
        else
        {
            menu_show_action_result(st, MENU_NOTIFICATION_ERROR, "Send failed");
        }
        return;
    }

    menu_line_copy_or_default(line0, sent_text, "Message sent");
    menu_line_copy_or_default(line1, "Waiting for ACK", "");
    menu_open_info_modal(st, "TX SENT", line0, line1, "Any key=back");
    menu_line_copy_or_default(st->tx_sent_text, line0, "");
    st->tx_ack_waiting = true;
}

static void menu_handle_notification(menu_state_t *st, const menu_notification_t *n)
{
    char code_line[MENU_LINE_BUF_SIZE];
    char source_line[MENU_LINE_BUF_SIZE];
    char text_safe[MENU_LINE_BUF_SIZE];
    bool has_text;

    if ((st == NULL) || (n == NULL))
    {
        return;
    }
    if ((n->type == MENU_NOTIFICATION_DELIVERY) &&
        st->tx_ack_waiting &&
        (st->modal == MENU_MODAL_INFO))
    {
        st->tx_ack_waiting = false;
        menu_render_popup("TX SENT", st->tx_sent_text, "Delivery ACK", "Any key=back");
        return;
    }
    if (st->modal != MENU_MODAL_NONE)
    {
        if (menu_modal_is_preemptible(st->modal))
        {
            st->modal = MENU_MODAL_NONE;
        }
        else
        {
            return;
        }
    }

    memset(text_safe, 0, sizeof(text_safe));
    (void)menu_line_copy(text_safe, 0U, n->text, MENU_LINE_CHARS);
    has_text = (text_safe[0] != '\0');
    if (!has_text && (n->type != MENU_NOTIFICATION_PAIRING))
    {
        return;
    }

    if (n->type == MENU_NOTIFICATION_RX)
    {
        uint32_t source_id = (n->reply_device_code != 0UL) ? n->reply_device_code : n->device_code;

        if (menu_should_open_quick_reply(has_text ? text_safe : NULL))
        {
            st->modal = MENU_MODAL_QUICK_REPLY;
            st->quick_reply_src_id = source_id;
            st->quick_reply_deadline_ms = HAL_GetTick() + 30000UL;
            st->quick_reply_last_seconds = 30U;
            menu_line_copy_or_default(st->quick_reply_text, text_safe, "");
            menu_render_quick_reply(st);
            return;
        }

        menu_line_format_source(source_line, source_id);
        menu_open_info_modal(st, "RX MESSAGE", has_text ? text_safe : "(empty)", source_line, "Any key=back");
        return;
    }

    if ((!st->popup_enabled) && (n->type != MENU_NOTIFICATION_PAIRING))
    {
        return;
    }

    if ((n->type == MENU_NOTIFICATION_PAIRING) &&
        (strncmp(text_safe, "PAIR_REQ", 8U) == 0))
    {
        st->modal = MENU_MODAL_PAIR_REQUEST;
        menu_line_clear(code_line);
        (void)menu_line_copy(code_line, 0U, "Code: ", 6U);
        (void)menu_line_copy(code_line, 6U, menu_pair_code_text(text_safe, 8U), 11U);
        menu_render_popup("PAIR REQUEST", code_line, "OK=accept", "Hold OK=reject");
        return;
    }

    if ((n->type == MENU_NOTIFICATION_PAIRING) &&
        (strncmp(text_safe, "PAIR_SENT", 9U) == 0))
    {
        menu_line_clear(code_line);
        (void)menu_line_copy(code_line, 0U, "Code: ", 6U);
        (void)menu_line_copy(code_line, 6U, menu_pair_code_text(text_safe, 9U), 11U);
        menu_open_info_modal(st, "PAIR REQ SENT", code_line, "Wait for PAIR_OK", "");
        return;
    }

    if ((n->type == MENU_NOTIFICATION_PAIRING) &&
        (strncmp(text_safe, "PAIR_OK", 7U) == 0))
    {
        menu_line_clear(code_line);
        (void)menu_line_copy(code_line, 0U, "Code: ", 6U);
        (void)menu_line_copy(code_line, 6U, menu_pair_code_text(text_safe, 7U), 11U);
        menu_open_info_modal(st, "PAIRING OK", code_line, "Device trusted", "");
        return;
    }

    if ((n->type == MENU_NOTIFICATION_PAIRING) &&
        (strncmp(text_safe, "PAIR_ERROR", 10U) == 0))
    {
        menu_open_info_modal(st, "PAIRING", "PAIR rejected", "Any key=back", "");
        return;
    }

    if (has_text)
    {
        menu_show_action_result(st, n->type, text_safe);
    }
    else
    {
        menu_show_action_result(st, n->type, "(empty)");
    }
}

static void menu_handle_pin_button(menu_state_t *st, button_event_t evt)
{
    menu_page_id_t target_page;
    menu_action_t pin_action;

    if ((st == NULL) || (evt == BUTTON_EVENT_NONE))
    {
        return;
    }

    if (evt == BUTTON_EVENT_UP_SHORT)
    {
        st->pin_digits[st->pin_index] = (uint8_t)((st->pin_digits[st->pin_index] + 1U) % 10U);
        menu_render_pin(st);
        return;
    }

    if (evt == BUTTON_EVENT_DOWN_SHORT)
    {
        st->pin_digits[st->pin_index] = (st->pin_digits[st->pin_index] == 0U) ?
                                        9U :
                                        (uint8_t)(st->pin_digits[st->pin_index] - 1U);
        menu_render_pin(st);
        return;
    }

    if (evt == BUTTON_EVENT_OK_LONG)
    {
        menu_close_modal(st);
        return;
    }

    if (evt != BUTTON_EVENT_OK_SHORT)
    {
        return;
    }

    if ((uint8_t)(st->pin_index + 1U) < MENU_SETTINGS_PIN_LEN)
    {
        st->pin_index++;
        menu_render_pin(st);
        return;
    }

    pin_action = st->pending_action;
    if (menu_is_pin_change_action(pin_action))
    {
        if (pin_action == MENU_ACTION_PIN_SET_ADMIN)
        {
            memcpy(s_admin_pin, st->pin_digits, sizeof(s_admin_pin));
            st->admin_unlock_until_ms = 0UL;
        }
        else
        {
            memcpy(s_user_pin, st->pin_digits, sizeof(s_user_pin));
            st->user_unlock_until_ms = 0UL;
        }
        st->modal = MENU_MODAL_NONE;
        st->pending_action = MENU_ACTION_NONE;
        menu_clear_pin_state(st);
        menu_show_action_result(st, MENU_NOTIFICATION_SECURITY, "PIN changed");
        return;
    }

    if (!menu_pin_matches(st))
    {
        memset(st->pin_digits, 0, sizeof(st->pin_digits));
        st->pin_index = 0U;
        menu_render_popup("BAD PIN", "Try again", "UP/DN digit", "OK=next HOLD=cancel");
        return;
    }

    target_page = st->pending_auth_page;
    if (st->pending_auth_level == MENU_AUTH_ADMIN)
    {
        st->admin_unlock_until_ms = HAL_GetTick() + MENU_SETTINGS_PIN_UNLOCK_MS;
        st->user_unlock_until_ms = st->admin_unlock_until_ms;
    }
    else if (st->pending_auth_level == MENU_AUTH_USER)
    {
        st->user_unlock_until_ms = HAL_GetTick() + MENU_SETTINGS_PIN_UNLOCK_MS;
    }
    st->modal = MENU_MODAL_NONE;
    menu_clear_pin_state(st);

    if (target_page == MENU_PAGE_NONE)
    {
        if (st->current_page == MENU_PAGE_NONE)
        {
            (void)lcd_main_set_mode(LCD_MODE_MONITOR);
        }
        else
        {
            menu_render(st);
        }
        return;
    }

    menu_open_page(st, target_page);
}

static void menu_handle_modal_button(menu_state_t *st, button_event_t evt)
{
    menu_action_t action;
    bool ok;
    bool send_pair_req;

    if ((st == NULL) || (evt == BUTTON_EVENT_NONE))
    {
        return;
    }

    switch (st->modal)
    {
        case MENU_MODAL_PIN:
            menu_handle_pin_button(st, evt);
            break;

        case MENU_MODAL_PAIR_REQUEST:
            if (evt == BUTTON_EVENT_OK_SHORT)
            {
                (void)radio_main_cmd_pairing_accept(true);
                menu_close_modal(st);
            }
            else if (evt == BUTTON_EVENT_OK_LONG)
            {
                (void)radio_main_cmd_pairing_accept(false);
                menu_close_modal(st);
            }
            break;

        case MENU_MODAL_PAIR_SETUP:
            send_pair_req = (!st->pairing_network_mode && (evt == BUTTON_EVENT_OK_LONG));
            if (st->pairing_network_mode)
            {
                if (evt != BUTTON_EVENT_OK_SHORT)
                {
                    menu_close_modal(st);
                    return;
                }
            }
            else if ((evt != BUTTON_EVENT_OK_SHORT) && (evt != BUTTON_EVENT_OK_LONG))
            {
                menu_close_modal(st);
                return;
            }

            ok = menu_start_pairing_session(st, send_pair_req, st->pairing_network_mode);
            if (!ok)
            {
                menu_show_action_result(st, MENU_NOTIFICATION_ERROR, "Pairing start failed");
            }
            break;

        case MENU_MODAL_SEND_CONFIRM:
            action = st->pending_action;
            menu_close_modal(st);
            if (evt == BUTTON_EVENT_OK_SHORT)
            {
                menu_execute_action(st, action);
            }
            break;

        case MENU_MODAL_QUICK_REPLY:
            if (evt == BUTTON_EVENT_UP_SHORT)
            {
                ok = radio_main_cmd_send_user_text("NO", st->quick_reply_src_id);
                menu_show_send_result(st, ok, "Sent NO");
            }
            else if (evt == BUTTON_EVENT_OK_SHORT)
            {
                ok = radio_main_cmd_send_user_text("OK", st->quick_reply_src_id);
                menu_show_send_result(st, ok, "Sent OK");
            }
            else if (evt == BUTTON_EVENT_DOWN_SHORT)
            {
                ok = radio_main_cmd_send_user_text("YES", st->quick_reply_src_id);
                menu_show_send_result(st, ok, "Sent YES");
            }
            else
            {
                menu_close_modal(st);
            }
            break;

        case MENU_MODAL_INFO:
        default:
            menu_close_modal(st);
            break;
    }
}

static void menu_handle_button(menu_state_t *st, button_event_t evt)
{
    const menu_page_t *page;
    radio_main_runtime_cfg_t radio_cfg;

    if ((st == NULL) || (evt == BUTTON_EVENT_NONE))
    {
        return;
    }

    if (st->modal != MENU_MODAL_NONE)
    {
        menu_handle_modal_button(st, evt);
        return;
    }

    if (st->current_page == MENU_PAGE_NONE)
    {
        if (evt == BUTTON_EVENT_UP_SHORT)
        {
            (void)lcd_main_monitor_scroll_up();
        }
        else if (evt == BUTTON_EVENT_DOWN_SHORT)
        {
            (void)lcd_main_monitor_scroll_down();
        }
        else if ((evt == BUTTON_EVENT_OK_SHORT) || (evt == BUTTON_EVENT_OK_LONG))
        {
            menu_request_pin(st, MENU_PAGE_PAGER, MENU_AUTH_USER);
        }
        return;
    }

    page = menu_get_page(st->current_page);
    if (page == NULL)
    {
        menu_enter_monitor(st);
        return;
    }

    if ((menu_page_auth_level(st->current_page) != MENU_AUTH_NONE) &&
        !menu_auth_unlocked(st, menu_page_auth_level(st->current_page)) &&
        (evt != BUTTON_EVENT_OK_LONG))
    {
        menu_request_pin(st, st->current_page, menu_page_auth_level(st->current_page));
        return;
    }

    switch (evt)
    {
        case BUTTON_EVENT_UP_SHORT:
            if (page->item_count > 0U)
            {
                uint8_t start = st->selected_idx;

                do
                {
                    if (st->selected_idx == 0U)
                    {
                        st->selected_idx = (uint8_t)(page->item_count - 1U);
                    }
                    else
                    {
                        st->selected_idx = (uint8_t)(st->selected_idx - 1U);
                    }
                } while ((st->selected_idx != start) &&
                         !menu_item_is_selectable(st, page, st->selected_idx));
                menu_render(st);
            }
            break;

        case BUTTON_EVENT_DOWN_SHORT:
            if (page->item_count > 0U)
            {
                uint8_t start = st->selected_idx;

                do
                {
                    st->selected_idx = (uint8_t)((st->selected_idx + 1U) % page->item_count);
                } while ((st->selected_idx != start) &&
                         !menu_item_is_selectable(st, page, st->selected_idx));
                menu_render(st);
            }
            break;

        case BUTTON_EVENT_OK_SHORT:
            if (st->selected_idx < page->item_count)
            {
                const menu_item_t *item = &page->items[st->selected_idx];

                if (!menu_item_is_selectable(st, page, st->selected_idx))
                {
                    break;
                }
                if ((st->current_page == MENU_PAGE_RADIO_SETTINGS) &&
                    radio_main_get_runtime_cfg(&radio_cfg))
                {
                    if (st->selected_idx == (uint8_t)(menu_radio_settings_item_count(&radio_cfg) - 1U))
                    {
                        menu_execute_action(st, MENU_ACTION_BACK);
                        break;
                    }
                }
                if ((st->current_page == MENU_PAGE_DEVICE_DELETE_LIST) &&
                    (st->selected_idx < MENU_TRUSTED_DEVICE_SLOTS))
                {
                    trusted_info_t info;

                    memset(&info, 0, sizeof(info));
                    if (menu_get_trusted_slot(st->selected_idx, &info))
                    {
                        menu_open_device_delete_action(st, st->selected_idx);
                    }
                    else
                    {
                        menu_show_action_result(st, MENU_NOTIFICATION_WARNING, "Empty slot");
                    }
                }
                else if (st->current_page == MENU_PAGE_SEND_TARGET_LIST)
                {
                    if (st->selected_idx < MENU_TRUSTED_DEVICE_SLOTS)
                    {
                        trusted_info_t info;
                        char label[MENU_LINE_BUF_SIZE];

                        memset(&info, 0, sizeof(info));
                        if (menu_get_trusted_slot(st->selected_idx, &info) &&
                            menu_is_send_target_allowed(info.node_id))
                        {
                            st->pending_target_node_id = info.node_id;
                            menu_build_trusted_slot_label(st->selected_idx, &info, label);
                            menu_open_send_prompt(st, st->send_target_action, label);
                        }
                        else
                        {
                            menu_show_action_result(st, MENU_NOTIFICATION_WARNING, "Empty slot");
                        }
                    }
                    else
                    {
                        menu_execute_action(st, MENU_ACTION_BACK);
                    }
                }
                else if ((st->current_page == MENU_PAGE_SEND_DIRECT_LIST) &&
                         (st->selected_idx < MENU_TRUSTED_DEVICE_SLOTS))
                {
                    trusted_info_t info;
                    char label[MENU_LINE_BUF_SIZE];

                    memset(&info, 0, sizeof(info));
                    if (menu_get_trusted_slot(st->selected_idx, &info) &&
                        menu_is_send_target_allowed(info.node_id))
                    {
                        st->pending_target_node_id = info.node_id;
                        menu_build_trusted_slot_label(st->selected_idx, &info, label);
                        menu_open_send_prompt(st, MENU_ACTION_SEND_DIRECT_DEFAULT, label);
                    }
                    else
                    {
                        menu_show_action_result(st, MENU_NOTIFICATION_WARNING, "Empty slot");
                    }
                }
                else if (item->child_page != MENU_PAGE_NONE)
                {
                    if ((menu_page_auth_level(item->child_page) != MENU_AUTH_NONE) &&
                        !menu_auth_unlocked(st, menu_page_auth_level(item->child_page)))
                    {
                        menu_request_pin(st, item->child_page, menu_page_auth_level(item->child_page));
                        break;
                    }
                    menu_open_page(st, item->child_page);
                }
                else if (menu_is_send_action(item->action))
                {
                    menu_open_send_target_page(st, item->action);
                }
                else
                {
                    menu_execute_action(st, item->action);
                }
            }
            break;

        case BUTTON_EVENT_OK_LONG:
            if ((st->current_page == MENU_PAGE_SEND_TARGET_LIST) &&
                (st->transient_parent_page != MENU_PAGE_NONE))
            {
                menu_open_page(st, st->transient_parent_page);
                break;
            }
            if (page->parent == MENU_PAGE_NONE)
            {
                menu_enter_monitor(st);
            }
            else
            {
                menu_open_page(st, page->parent);
            }
            break;

        default:
            break;
    }
}

static void menu_execute_action(menu_state_t *st, menu_action_t action)
{
    char line0[MENU_LINE_BUF_SIZE];
    char line1[MENU_LINE_BUF_SIZE];
    security_runtime_cfg_t cfg;
    radio_main_runtime_cfg_t radio_cfg;
    trusted_info_t info;
    bmp280_api_data_t bmp;
    bool tpm_ready;
    bool send_ok;
    bool ok;
    bool have_radio_cfg;
    uint8_t idx;
    uint8_t count;
    uint32_t auto_ping_ms;
    int32_t distance_mm;
    uint32_t dst_id;

    menu_line_clear(line0);
    menu_line_clear(line1);
    memset(&cfg, 0, sizeof(cfg));
    memset(&radio_cfg, 0, sizeof(radio_cfg));
    memset(&info, 0, sizeof(info));
    have_radio_cfg = radio_main_get_runtime_cfg(&radio_cfg);

    if (menu_execute_radio_action(st, action))
    {
        return;
    }

    if (menu_is_send_action(action) &&
        !menu_is_send_target_allowed(st->pending_target_node_id))
    {
        st->send_target_action = MENU_ACTION_NONE;
        menu_show_action_result(st, MENU_NOTIFICATION_WARNING, "No target");
        return;
    }

    switch (action)
    {
        case MENU_ACTION_BACK:
            if ((st->current_page == MENU_PAGE_SEND_TARGET_LIST) &&
                (st->transient_parent_page != MENU_PAGE_NONE))
            {
                menu_open_page(st, st->transient_parent_page);
                break;
            }
            {
                const menu_page_t *page = menu_get_page(st->current_page);

                if ((page == NULL) || (page->parent == MENU_PAGE_NONE))
                {
                    menu_enter_monitor(st);
                }
                else
                {
                    menu_open_page(st, page->parent);
                }
            }
            break;

        case MENU_ACTION_EXIT_TO_MONITOR:
            menu_enter_monitor(st);
            break;

        case MENU_ACTION_SEND_DEFAULT:
            dst_id = st->pending_target_node_id;
            send_ok = radio_main_cmd_send_template(1U, 0U, dst_id);
            menu_show_send_result(st, send_ok, "Sent STS:OK");
            st->pending_target_node_id = 0U;
            st->send_target_action = MENU_ACTION_NONE;
            break;

        case MENU_ACTION_SEND_DIRECT_DEFAULT:
            if (st->pending_target_node_id == 0U)
            {
                menu_show_action_result(st, MENU_NOTIFICATION_WARNING, "No target");
                break;
            }
            send_ok = radio_main_cmd_send_template(1U, 0U, st->pending_target_node_id);
            menu_line_format_device_ref(line0, "Sent to ", st->pending_target_node_id);
            menu_show_send_result(st, send_ok, line0);
            st->pending_target_node_id = 0U;
            st->send_target_action = MENU_ACTION_NONE;
            break;

        case MENU_ACTION_SEND_ALERT_FIRE:
            dst_id = st->pending_target_node_id;
            send_ok = radio_main_cmd_send_template(0U, 0U, dst_id);
            menu_show_send_result(st, send_ok, "Sent ALR:FIRE");
            st->pending_target_node_id = 0U;
            st->send_target_action = MENU_ACTION_NONE;
            break;

        case MENU_ACTION_SEND_ALERT_INTR:
            dst_id = st->pending_target_node_id;
            send_ok = radio_main_cmd_send_template(0U, 1U, dst_id);
            menu_show_send_result(st, send_ok, "Sent ALR:INTR");
            st->pending_target_node_id = 0U;
            st->send_target_action = MENU_ACTION_NONE;
            break;

        case MENU_ACTION_SEND_ALERT_LOWBATT:
            dst_id = st->pending_target_node_id;
            send_ok = radio_main_cmd_send_template(0U, 2U, dst_id);
            menu_show_send_result(st, send_ok, "Sent ALR:LOW");
            st->pending_target_node_id = 0U;
            st->send_target_action = MENU_ACTION_NONE;
            break;

        case MENU_ACTION_SEND_STATUS_OK:
            dst_id = st->pending_target_node_id;
            send_ok = radio_main_cmd_send_template(1U, 0U, dst_id);
            menu_show_send_result(st, send_ok, "Sent STS:OK");
            st->pending_target_node_id = 0U;
            st->send_target_action = MENU_ACTION_NONE;
            break;

        case MENU_ACTION_SEND_STATUS_BUSY:
            dst_id = st->pending_target_node_id;
            send_ok = radio_main_cmd_send_template(1U, 1U, dst_id);
            menu_show_send_result(st, send_ok, "Sent STS:BUSY");
            st->pending_target_node_id = 0U;
            st->send_target_action = MENU_ACTION_NONE;
            break;

        case MENU_ACTION_SEND_STATUS_IDLE:
            dst_id = st->pending_target_node_id;
            send_ok = radio_main_cmd_send_template(1U, 2U, dst_id);
            menu_show_send_result(st, send_ok, "Sent STS:IDLE");
            st->pending_target_node_id = 0U;
            st->send_target_action = MENU_ACTION_NONE;
            break;

        case MENU_ACTION_SEND_SERVICE_PING:
            dst_id = st->pending_target_node_id;
            send_ok = radio_main_cmd_send_template(2U, 0U, dst_id);
            menu_show_send_result(st, send_ok, "Sent SRV:PING");
            st->pending_target_node_id = 0U;
            st->send_target_action = MENU_ACTION_NONE;
            break;

        case MENU_ACTION_SEND_SERVICE_RESET:
            dst_id = st->pending_target_node_id;
            send_ok = radio_main_cmd_send_template(2U, 1U, dst_id);
            menu_show_send_result(st, send_ok, "Sent SRV:RESET");
            st->pending_target_node_id = 0U;
            st->send_target_action = MENU_ACTION_NONE;
            break;

        case MENU_ACTION_SEND_SERVICE_SYNC:
            dst_id = st->pending_target_node_id;
            send_ok = radio_main_cmd_send_template(2U, 2U, dst_id);
            menu_show_send_result(st, send_ok, "Sent SRV:SYNC");
            st->pending_target_node_id = 0U;
            st->send_target_action = MENU_ACTION_NONE;
            break;

        case MENU_ACTION_SEND_ASK_DONE:
            dst_id = st->pending_target_node_id;
            send_ok = radio_main_cmd_send_user_text("ASK: Is done?", dst_id);
            menu_show_send_result(st, send_ok, "Sent ASK:DONE");
            st->pending_target_node_id = 0U;
            st->send_target_action = MENU_ACTION_NONE;
            break;

        case MENU_ACTION_SEND_ACT_COME_OVER:
            dst_id = st->pending_target_node_id;
            send_ok = radio_main_cmd_send_user_text("ACT Come over.", dst_id);
            menu_show_send_result(st, send_ok, "Sent ACT:COME");
            st->pending_target_node_id = 0U;
            st->send_target_action = MENU_ACTION_NONE;
            break;

        case MENU_ACTION_SEND_ACT_STOP:
            dst_id = st->pending_target_node_id;
            send_ok = radio_main_cmd_send_user_text("ACT: Stop!", dst_id);
            menu_show_send_result(st, send_ok, "Sent ACT:STOP");
            st->pending_target_node_id = 0U;
            st->send_target_action = MENU_ACTION_NONE;
            break;

        case MENU_ACTION_SEND_ASK_READY:
            dst_id = st->pending_target_node_id;
            send_ok = radio_main_cmd_send_user_text("ASK: Is ready?", dst_id);
            menu_show_send_result(st, send_ok, "Sent ASK:READY");
            st->pending_target_node_id = 0U;
            st->send_target_action = MENU_ACTION_NONE;
            break;

        case MENU_ACTION_DEVICE_ADD:
            st->pairing_network_mode = false;
            st->modal = MENU_MODAL_PAIR_SETUP;
            st->pending_action = MENU_ACTION_NONE;
            menu_render_popup("PAIR MODE 5 min", "OK=listen", "Hold OK=PAIR_REQ", "Any key=cancel");
            break;

        case MENU_ACTION_DEVICE_ADD_NETWORK:
            if (menu_gateway_slot_in_use())
            {
                menu_show_action_result(st, MENU_NOTIFICATION_WARNING, "Remove G first");
                break;
            }
            st->pairing_network_mode = true;
            st->modal = MENU_MODAL_PAIR_SETUP;
            st->pending_action = MENU_ACTION_NONE;
            menu_render_popup("NET PAIR 5 min", "OK=listen", "GW sends PAIR_REQ", "Any key=cancel");
            break;

        case MENU_ACTION_DEVICE_DELETE:
            menu_open_page(st, MENU_PAGE_DEVICE_DELETE_LIST);
            break;

        case MENU_ACTION_DEVICE_DELETE_CONFIRM:
            if (st->selected_device_slot >= MENU_TRUSTED_DEVICE_SLOTS)
            {
                menu_show_action_result(st, MENU_NOTIFICATION_WARNING, "No device");
                break;
            }
            if (!security_main_cmd_get_device(st->selected_device_slot, &info) || !info.in_use)
            {
                st->current_page = MENU_PAGE_DEVICE_DELETE_LIST;
                st->selected_idx = 0U;
                menu_show_action_result(st, MENU_NOTIFICATION_WARNING, "Device gone");
                break;
            }

            ok = security_main_cmd_delete_device(info.node_id);
            if (ok)
            {
                send_ok = radio_main_cmd_send_pair_error(info.node_id);
            }
            else
            {
                send_ok = false;
            }

            st->current_page = MENU_PAGE_DEVICE_DELETE_LIST;
            st->selected_idx = 0U;
            st->selected_device_slot = MENU_DEVICE_SLOT_INVALID;

            if (!ok)
            {
                menu_show_action_result(st, MENU_NOTIFICATION_ERROR, "Delete failed");
            }
            else if (send_ok)
            {
                menu_show_action_result(st, MENU_NOTIFICATION_SECURITY, "Device removed");
            }
            else
            {
                menu_show_action_result(st, MENU_NOTIFICATION_WARNING, "Removed local only");
            }
            break;

        case MENU_ACTION_DEVICE_INFO:
            count = 0U;
            for (idx = 1U; idx <= MENU_TRUSTED_NODE_SLOTS; idx++)
            {
                if (security_main_cmd_get_device(idx, &info) &&
                    info.in_use &&
                    (info.node_id != LAVIET_GATEWAY_ID) &&
                    (info.node_id != LAVIET_BROADCAST_ID) &&
                    (info.node_id != 0U))
                {
                    count++;
                }
            }
            menu_line_copy_or_default(line0,
                                      menu_gateway_slot_in_use() ? "Gateway connected" : "Gateway missing",
                                      "");
            menu_line_format_u32(line1, "Nodes ", count, "/15");
            menu_open_info_modal(st, "CONNECTION INFO", line0, line1, "Any key=back");
            break;

        case MENU_ACTION_PIN_TOGGLE:
            s_pin_enabled = !s_pin_enabled;
            if (!s_pin_enabled)
            {
                st->user_unlock_until_ms = 0UL;
                st->admin_unlock_until_ms = 0UL;
            }
            menu_show_action_result(st,
                                    MENU_NOTIFICATION_SECURITY,
                                    s_pin_enabled ? "PIN enabled" : "PIN disabled");
            break;

        case MENU_ACTION_PIN_SET_USER:
        case MENU_ACTION_PIN_SET_ADMIN:
            menu_start_pin_change(st, action);
            break;

        case MENU_ACTION_SEC_TOGGLE_FH:
            if (security_main_cmd_get_runtime_cfg(&cfg))
            {
                const char *state_text = "FH OFF";

                bool new_state = !cfg.fh_enabled;
                ok = security_main_cmd_set_fh(new_state) && radio_main_cmd_set_fh(new_state);
                if (new_state)
                {
                    state_text = "FH ON";
                }
                menu_show_ok_or_error(st, ok, state_text, "FH set failed");
            }
            break;

        case MENU_ACTION_SEC_FH_ENABLE:
            if (have_radio_cfg && (radio_cfg.active_modulation != RADIO_MAIN_MODULATION_LORA))
            {
                menu_show_action_result(st, MENU_NOTIFICATION_WARNING, "FH needs LoRa mode");
                break;
            }
            ok = security_main_cmd_set_fh(true) && radio_main_cmd_set_fh(true);
            menu_show_ok_or_error(st, ok, "FH enabled", "FH enable failed");
            break;

        case MENU_ACTION_SEC_FH_DISABLE:
            ok = security_main_cmd_set_fh(false) && radio_main_cmd_set_fh(false);
            menu_show_ok_or_error(st, ok, "FH disabled", "FH disable failed");
            break;

        case MENU_ACTION_SEC_FH_PERIOD_1000:
            ok = security_main_cmd_set_fh_period(1000UL) && radio_main_cmd_set_fh_period(1000UL);
            menu_show_ok_or_error(st, ok, "FH cycle 1 sec", "FH cycle failed");
            break;

        case MENU_ACTION_SEC_FH_PERIOD_2000:
            ok = security_main_cmd_set_fh_period(2000UL) && radio_main_cmd_set_fh_period(2000UL);
            menu_show_ok_or_error(st, ok, "FH cycle 2 sec", "FH cycle failed");
            break;

        case MENU_ACTION_SEC_FH_PERIOD_5000:
            ok = security_main_cmd_set_fh_period(5000UL) && radio_main_cmd_set_fh_period(5000UL);
            menu_show_ok_or_error(st, ok, "FH cycle 5 sec", "FH cycle failed");
            break;

        case MENU_ACTION_SEC_FH_PERIOD_10000:
            ok = security_main_cmd_set_fh_period(10000UL) && radio_main_cmd_set_fh_period(10000UL);
            menu_show_ok_or_error(st, ok, "FH cycle 10 sec", "FH cycle failed");
            break;

        case MENU_ACTION_SEC_TPM_INFO:
            if (security_main_get_tpm_ready(&tpm_ready))
            {
                if (tpm_ready)
                {
                    menu_open_info_modal(st, "TPM", "Ready", "Any key=back", "");
                }
                else
                {
                    menu_open_info_modal(st, "TPM", "Not ready", "Any key=back", "");
                }
            }
            else
            {
                menu_show_action_result(st, MENU_NOTIFICATION_ERROR, "TPM query failed");
            }
            break;

        case MENU_ACTION_SEC_ROTATE_KEYS:
            ok = security_main_cmd_rotate_key();
            menu_show_ok_or_error(st, ok, "Key rotated", "Keys: not ready");
            break;

        case MENU_ACTION_SEC_CODING_ON:
            ok = security_main_cmd_set_coding(true) && radio_main_cmd_set_coding(true);
            menu_show_ok_or_error(st, ok, "Coding ON", "Coding set failed");
            break;

        case MENU_ACTION_SEC_CODING_OFF:
            ok = security_main_cmd_set_coding(false) && radio_main_cmd_set_coding(false);
            menu_show_ok_or_error(st, ok, "Coding OFF", "Coding set failed");
            break;

        case MENU_ACTION_SEC_NOTIFY_POPUP:
            ok = security_main_cmd_set_notify_mode(SECURITY_NOTIFY_POPUP);
            if (ok)
            {
                st->popup_enabled = true;
            }
            menu_show_ok_or_error(st, ok, "Notif POPUP", "Notif set failed");
            break;

        case MENU_ACTION_SEC_NOTIFY_BADGE:
            ok = security_main_cmd_set_notify_mode(SECURITY_NOTIFY_BADGE);
            if (ok)
            {
                st->popup_enabled = false;
            }
            menu_show_ok_or_error(st, ok, "Notif BADGE", "Notif set failed");
            break;

        case MENU_ACTION_SEC_AUTOPING_ON:
            ok = security_main_cmd_set_auto_ping(true) && radio_main_cmd_set_auto_ping(true);
            if (ok && radio_main_get_auto_ping_period_ms(&auto_ping_ms))
            {
                menu_line_format_u32(line1, "Period ", auto_ping_ms, " ms");
                menu_open_info_modal(st, "AUTOPING ON", line1, "Any key=back", "");
            }
            else
            {
                menu_show_ok_or_error(st, ok, "AutoPing ON", "AutoPing failed");
            }
            break;

        case MENU_ACTION_SEC_AUTOPING_OFF:
            ok = security_main_cmd_set_auto_ping(false) && radio_main_cmd_set_auto_ping(false);
            menu_show_ok_or_error(st, ok, "AutoPing OFF", "AutoPing failed");
            break;

        case MENU_ACTION_SEC_TOGGLE_CODING:
            if (security_main_cmd_get_runtime_cfg(&cfg))
            {
                const char *state_text = "Coding OFF";

                bool new_state = !cfg.coding_enabled;
                ok = security_main_cmd_set_coding(new_state) && radio_main_cmd_set_coding(new_state);
                if (new_state)
                {
                    state_text = "Coding ON";
                }
                menu_show_ok_or_error(st, ok, state_text, "Coding set failed");
            }
            break;

        case MENU_ACTION_SEC_TOGGLE_NOTIFY_MODE:
            if (security_main_cmd_get_runtime_cfg(&cfg))
            {
                security_notify_mode_t mode = SECURITY_NOTIFY_POPUP;

                if (cfg.notify_mode == SECURITY_NOTIFY_POPUP)
                {
                    mode = SECURITY_NOTIFY_BADGE;
                }
                ok = security_main_cmd_set_notify_mode(mode);
                if (ok)
                {
                    if (mode == SECURITY_NOTIFY_POPUP)
                    {
                        st->popup_enabled = true;
                    }
                    else
                    {
                        st->popup_enabled = false;
                    }
                }

                if (ok)
                {
                    if (st->popup_enabled)
                    {
                        menu_show_action_result(st, MENU_NOTIFICATION_SECURITY, "Notif POPUP");
                    }
                    else
                    {
                        menu_show_action_result(st, MENU_NOTIFICATION_SECURITY, "Notif BADGE");
                    }
                }
                else
                {
                    menu_show_action_result(st, MENU_NOTIFICATION_ERROR, "Notif set failed");
                }
            }
            break;

        case MENU_ACTION_SEC_TOGGLE_AUTOPING:
            if (security_main_cmd_get_runtime_cfg(&cfg))
            {
                const char *state_text = "AutoPing OFF";

                bool new_state = !cfg.auto_ping_enabled;
                ok = security_main_cmd_set_auto_ping(new_state) && radio_main_cmd_set_auto_ping(new_state);
                if (new_state)
                {
                    state_text = "AutoPing ON";
                }
                menu_show_ok_or_error(st, ok, state_text, "AutoPing failed");
            }
            break;

        case MENU_ACTION_HW_MEASURE_DIST:
            distance_mm = tof_main_get_last_distance();
            menu_line_format_i32(line1, "", distance_mm, " mm");
            menu_open_info_modal(st, "Distance", line1, "Any key=back", "");
            break;

        case MENU_ACTION_HW_MEASURE_TEMP:
            if (bmp280_main_get_last(&bmp))
            {
                menu_line_format_fixed2(line0, "Temp ", bmp.temperature_c, " C");
                menu_open_info_modal(st, "BMP280", line0, "Any key=back", "");
            }
            else
            {
                menu_open_info_modal(st, "BMP280", "No data", "Any key=back", "");
            }
            break;

        case MENU_ACTION_HW_MEASURE_PRESS:
            if (bmp280_main_get_last(&bmp))
            {
                menu_line_format_fixed2(line0, "Press ", bmp.pressure_hpa, " hPa");
                menu_open_info_modal(st, "BMP280", line0, "Any key=back", "");
            }
            else
            {
                menu_open_info_modal(st, "BMP280", "No data", "Any key=back", "");
            }
            break;

        case MENU_ACTION_HW_LED_RAINBOW:
            st->led_mode = 0U;
            (void)led_array_start_rainbow(15U, 5U, 100U);
            menu_show_action_result(st, MENU_NOTIFICATION_SECURITY, "LED rainbow");
            break;

        case MENU_ACTION_HW_LED_BREATH:
            st->led_mode = 1U;
            (void)led_array_start_breath(LED_ARRAY_LED_ALL, 1200U, 5U, 100U);
            menu_show_action_result(st, MENU_NOTIFICATION_SECURITY, "LED breath");
            break;

        case MENU_ACTION_HW_LED_OFF:
            st->led_mode = 2U;
            (void)led_array_stop_effect();
            (void)led_array_off(LED_ARRAY_LED_ALL);
            menu_show_action_result(st, MENU_NOTIFICATION_SECURITY, "LED off");
            break;

        case MENU_ACTION_HW_LED_MODE:
            st->led_mode = (uint8_t)((st->led_mode + 1U) % 3U);
            if (st->led_mode == 0U)
            {
                (void)led_array_start_rainbow(15U, 5U, 100U);
                menu_show_action_result(st, MENU_NOTIFICATION_SECURITY, "LED rainbow");
            }
            else if (st->led_mode == 1U)
            {
                (void)led_array_start_breath(LED_ARRAY_LED_ALL, 1200U, 5U, 100U);
                menu_show_action_result(st, MENU_NOTIFICATION_SECURITY, "LED breath");
            }
            else
            {
                (void)led_array_stop_effect();
                (void)led_array_off(LED_ARRAY_LED_ALL);
                menu_show_action_result(st, MENU_NOTIFICATION_SECURITY, "LED off");
            }
            break;

        case MENU_ACTION_HW_RADIO_RESET:
            ok = radio_main_cmd_reset_module();
            menu_show_ok_or_error(st, ok, "SX1276 reset OK", "SX1276 reset fail");
            break;

        case MENU_ACTION_MOD_LORA_STD:
            ok = radio_main_cmd_set_modulation((uint8_t)RADIO_MAIN_MODULATION_LORA) &&
                 radio_main_cmd_set_lora_preset(0U) &&
                 security_main_cmd_set_lora_preset(0U);
            menu_show_ok_or_error(st, ok, "LoRa STD", "LoRa preset failed");
            break;

        case MENU_ACTION_MOD_LORA_RANGE:
            ok = radio_main_cmd_set_modulation((uint8_t)RADIO_MAIN_MODULATION_LORA) &&
                 radio_main_cmd_set_lora_preset(1U) &&
                 security_main_cmd_set_lora_preset(1U);
            menu_show_ok_or_error(st, ok, "LoRa RANGE", "LoRa preset failed");
            break;

        case MENU_ACTION_MOD_LORA_FAST:
            ok = radio_main_cmd_set_modulation((uint8_t)RADIO_MAIN_MODULATION_LORA) &&
                 radio_main_cmd_set_lora_preset(2U) &&
                 security_main_cmd_set_lora_preset(2U);
            menu_show_ok_or_error(st, ok, "LoRa FAST", "LoRa preset failed");
            break;

        case MENU_ACTION_MOD_FSK:
            ok = radio_main_cmd_set_modulation((uint8_t)RADIO_MAIN_MODULATION_FSK);
            menu_show_ok_or_error(st, ok, "FSK mode", "FSK set failed");
            break;

        case MENU_ACTION_MOD_OOK:
            ok = radio_main_cmd_set_modulation((uint8_t)RADIO_MAIN_MODULATION_OOK);
            menu_show_ok_or_error(st, ok, "OOK mode", "OOK set failed");
            break;

        case MENU_ACTION_INFO_SHOW:
            menu_line_format_hex32(line0, "Node ", radio_main_get_node_id());
            menu_open_info_modal(st, "BEKO W1", line0, "Menu/Sec active", "Any key=back");
            break;

        default:
            break;
    }
}

static bool menu_execute_radio_action(menu_state_t *st, menu_action_t action)
{
    size_t idx;

    for (idx = 0U; idx < (sizeof(s_radio_action_bindings) / sizeof(s_radio_action_bindings[0])); idx++)
    {
        const menu_radio_action_binding_t *binding = &s_radio_action_bindings[idx];
        radio_main_runtime_cfg_t runtime_cfg;
        bool ok = true;

        if (binding->action != action)
        {
            continue;
        }

        memset(&runtime_cfg, 0, sizeof(runtime_cfg));
        if (binding->set_modulation)
        {
            if (!radio_main_get_runtime_cfg(&runtime_cfg) ||
                (runtime_cfg.active_modulation != binding->modulation))
            {
                ok = radio_main_cmd_set_modulation((uint8_t)binding->modulation);
            }
        }

        if (ok && binding->set_option)
        {
            ok = radio_main_cmd_set_option(binding->option, binding->value);
        }

        if (ok && binding->persist_lora_preset)
        {
            ok = security_main_cmd_set_lora_preset((uint8_t)binding->value);
        }

        if (ok && radio_main_get_runtime_cfg(&runtime_cfg))
        {
            ok = security_main_cmd_set_radio_runtime_cfg(&runtime_cfg);
        }

        menu_show_ok_or_error(st, ok, binding->ok_text, binding->err_text);
        return true;
    }

    return false;
}

static const menu_page_t *menu_get_page(menu_page_id_t page_id)
{
    size_t idx;

    if (page_id == MENU_PAGE_NONE)
    {
        return NULL;
    }

    idx = (size_t)page_id - 1U;
    if (idx >= (sizeof(s_pages) / sizeof(s_pages[0])))
    {
        return NULL;
    }

    return &s_pages[idx];
}

/* Only send-type actions require a confirmation modal. */
static bool menu_is_send_action(menu_action_t action)
{
    switch (action)
    {
        case MENU_ACTION_SEND_DEFAULT:
        case MENU_ACTION_SEND_ALERT_FIRE:
        case MENU_ACTION_SEND_ALERT_INTR:
        case MENU_ACTION_SEND_ALERT_LOWBATT:
        case MENU_ACTION_SEND_STATUS_OK:
        case MENU_ACTION_SEND_STATUS_BUSY:
        case MENU_ACTION_SEND_STATUS_IDLE:
        case MENU_ACTION_SEND_SERVICE_PING:
        case MENU_ACTION_SEND_SERVICE_RESET:
        case MENU_ACTION_SEND_SERVICE_SYNC:
        case MENU_ACTION_SEND_ASK_DONE:
        case MENU_ACTION_SEND_ACT_COME_OVER:
        case MENU_ACTION_SEND_ACT_STOP:
        case MENU_ACTION_SEND_ASK_READY:
            return true;
        default:
            return false;
    }
}

static bool menu_is_send_target_allowed(uint32_t node_id)
{
    return ((node_id != 0UL) && (node_id != LAVIET_BROADCAST_ID));
}

static bool menu_item_is_selectable(const menu_state_t *st, const menu_page_t *page, uint8_t item_idx)
{
    radio_main_runtime_cfg_t radio_cfg;

    if ((st == NULL) || (page == NULL) || (item_idx >= page->item_count))
    {
        return false;
    }

    if ((st->current_page == MENU_PAGE_RADIO_SETTINGS) &&
        radio_main_get_runtime_cfg(&radio_cfg))
    {
        return (item_idx < menu_radio_settings_item_count(&radio_cfg));
    }

    if (st->current_page == MENU_PAGE_SEND_TARGET_LIST)
    {
        if (item_idx == (uint8_t)(page->item_count - 1U))
        {
            return true;
        }

        if (item_idx < MENU_TRUSTED_DEVICE_SLOTS)
        {
            trusted_info_t info;

            memset(&info, 0, sizeof(info));
            return (security_main_cmd_get_device(item_idx, &info) &&
                    info.in_use &&
                    menu_is_send_target_allowed(info.node_id));
        }

        return false;
    }

    if (st->current_page == MENU_PAGE_SEND_DIRECT_LIST)
    {
        if (item_idx == (uint8_t)(page->item_count - 1U))
        {
            return true;
        }

        if (item_idx < MENU_TRUSTED_DEVICE_SLOTS)
        {
            trusted_info_t info;

            memset(&info, 0, sizeof(info));
            return (security_main_cmd_get_device(item_idx, &info) &&
                    info.in_use &&
                    menu_is_send_target_allowed(info.node_id));
        }

        return false;
    }

    if ((st->current_page == MENU_PAGE_SECURITY_AUTOPING) &&
        (item_idx == 0U))
    {
        return false;
    }

    if ((st->current_page == MENU_PAGE_DEVICE_DELETE_ACTION) &&
        (item_idx == 0U))
    {
        return false;
    }

    return true;
}

static void menu_open_send_prompt(menu_state_t *st, menu_action_t action, const char *label)
{
    char msg[MENU_LINE_BUF_SIZE];

    if (st == NULL)
    {
        return;
    }

    menu_line_clear(msg);
    if (label != NULL)
    {
        (void)menu_line_copy(msg, 0U, label, MENU_LINE_CHARS);
    }
    else
    {
        (void)menu_line_copy(msg, 0U, "Message", MENU_LINE_CHARS);
    }

    st->modal = MENU_MODAL_SEND_CONFIRM;
    st->pending_action = action;
    menu_render_popup("SEND MESSAGE?", msg, "OK=send", "Any key=back");
}

static void menu_open_send_target_page(menu_state_t *st, menu_action_t action)
{
    menu_page_id_t previous_page;
    const menu_page_t *page;

    if (st == NULL)
    {
        return;
    }

    previous_page = st->current_page;
    st->current_page = MENU_PAGE_SEND_TARGET_LIST;
    st->selected_idx = 0U;
    st->modal = MENU_MODAL_NONE;
    st->pending_action = MENU_ACTION_NONE;
    st->pending_target_node_id = 0U;
    st->send_target_action = action;
    st->transient_parent_page = previous_page;
    page = menu_get_page(MENU_PAGE_SEND_TARGET_LIST);
    if (page != NULL)
    {
        while ((st->selected_idx < page->item_count) &&
               !menu_item_is_selectable(st, page, st->selected_idx))
        {
            st->selected_idx++;
        }
        if ((page->item_count > 0U) && (st->selected_idx >= page->item_count))
        {
            st->selected_idx = (uint8_t)(page->item_count - 1U);
        }
    }
    menu_render(st);
}

static void menu_open_device_delete_action(menu_state_t *st, uint8_t slot)
{
    if (st == NULL)
    {
        return;
    }

    st->current_page = MENU_PAGE_DEVICE_DELETE_ACTION;
    st->selected_idx = 1U;
    st->modal = MENU_MODAL_NONE;
    st->pending_action = MENU_ACTION_NONE;
    st->selected_device_slot = slot;
    menu_render(st);
}

/* Starts pairing and reports the mode/result through a modal message. */
static bool menu_start_pairing_session(menu_state_t *st, bool send_pair_req, bool network_mode)
{
    bool send_ok = true;

    if (st == NULL)
    {
        return false;
    }
    if (network_mode && menu_gateway_slot_in_use())
    {
        return false;
    }
    if (network_mode)
    {
        if (!radio_main_cmd_start_network_pairing(MENU_PAIRING_WINDOW_MS))
        {
            return false;
        }
        send_pair_req = false;
    }
    else if (!radio_main_cmd_start_pairing(MENU_PAIRING_WINDOW_MS))
    {
        return false;
    }

    if (send_pair_req)
    {
        if (network_mode)
        {
            send_ok = radio_main_cmd_send_network_pair_req();
        }
        else
        {
            send_ok = radio_main_cmd_send_pair_req();
        }
    }

    if (send_pair_req)
    {
        const char *status_line = "PAIR_REQ failed";

        if (send_ok)
        {
            status_line = "PAIR_REQ sent";
        }

        menu_open_info_modal(st,
                             network_mode ? "NET PAIR" : "PAIR MODE",
                             "5 min active",
                             status_line,
                             "Any key=close");
    }
    else
    {
        menu_open_info_modal(st,
                             network_mode ? "NET PAIR" : "PAIR MODE",
                             "5 min active",
                             network_mode ? "Wait PAIR_REQ GW" : "Listening...",
                             "Any key=close");
    }

    return true;
}
