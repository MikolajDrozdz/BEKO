#include "menu_main.h"

#include "beko_net_proto.h"
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

#include <stdio.h>
#include <string.h>

#define MENU_TASK_STACK_SIZE                6144U
#define MENU_TASK_STACK_WORDS               (MENU_TASK_STACK_SIZE / sizeof(StackType_t))
#define MENU_NOTIFY_QUEUE_DEPTH             8U
#define MENU_INACTIVITY_TIMEOUT_MS          60000UL
#define MENU_DISPLAY_ROWS                   4U
#define MENU_ITEMS_VISIBLE                  3U

typedef enum
{
    MENU_PAGE_NONE = 0,
    MENU_PAGE_PAGER,
    MENU_PAGE_MSG_GROUPS,
    MENU_PAGE_GROUP_ALERT,
    MENU_PAGE_GROUP_STATUS,
    MENU_PAGE_GROUP_SERVICE,
    MENU_PAGE_MAIN,
    MENU_PAGE_DEVICES,
    MENU_PAGE_SECURITY,
    MENU_PAGE_HARDWARE,
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
    MENU_ACTION_SEND_ALERT_FIRE,
    MENU_ACTION_SEND_ALERT_INTR,
    MENU_ACTION_SEND_ALERT_LOWBATT,
    MENU_ACTION_SEND_STATUS_OK,
    MENU_ACTION_SEND_STATUS_BUSY,
    MENU_ACTION_SEND_STATUS_IDLE,
    MENU_ACTION_SEND_SERVICE_PING,
    MENU_ACTION_SEND_SERVICE_RESET,
    MENU_ACTION_SEND_SERVICE_SYNC,
    MENU_ACTION_DEVICE_ADD,
    MENU_ACTION_DEVICE_DELETE,
    MENU_ACTION_DEVICE_INFO,
    MENU_ACTION_SEC_TOGGLE_FH,
    MENU_ACTION_SEC_TPM_INFO,
    MENU_ACTION_SEC_ROTATE_KEYS,
    MENU_ACTION_SEC_TOGGLE_CODING,
    MENU_ACTION_SEC_TOGGLE_NOTIFY_MODE,
    MENU_ACTION_SEC_TOGGLE_AUTOPING,
    MENU_ACTION_HW_MEASURE_DIST,
    MENU_ACTION_HW_MEASURE_TEMP,
    MENU_ACTION_HW_MEASURE_PRESS,
    MENU_ACTION_HW_LED_MODE,
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

typedef struct
{
    bool popup_active;
    bool pairing_prompt;
    bool pair_setup_prompt;
    bool send_prompt;
    menu_action_t pending_send_action;
    menu_page_id_t page_before_popup;
    bool popup_enabled;
    uint32_t last_input_ms;
    menu_page_id_t current_page;
    uint8_t selected_idx;
    uint8_t led_mode;
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

static void menu_main_task_fn(void *argument);
static const menu_page_t *menu_get_page(menu_page_id_t page_id);
static void menu_render(menu_state_t *st);
static void menu_render_popup(const char *l0, const char *l1, const char *l2, const char *l3);
static void menu_enter_monitor(menu_state_t *st);
static void menu_open_page(menu_state_t *st, menu_page_id_t page_id);
static void menu_handle_button(menu_state_t *st, button_event_t evt);
static void menu_handle_notification(menu_state_t *st, const menu_notification_t *n);
static void menu_execute_action(menu_state_t *st, menu_action_t action);
static void menu_notify_text(menu_notification_type_t type, const char *text);
static bool menu_execute_radio_action(menu_action_t action);
static bool menu_is_send_action(menu_action_t action);
static void menu_open_send_prompt(menu_state_t *st, menu_action_t action, const char *label);
static bool menu_start_pairing_session(menu_state_t *st, bool send_join_req);

static const menu_item_t s_page_pager_items[] =
{
    { "Send message", MENU_PAGE_NONE, MENU_ACTION_SEND_DEFAULT },
    { "Message groups", MENU_PAGE_MSG_GROUPS, MENU_ACTION_NONE },
    { "Main menu", MENU_PAGE_MAIN, MENU_ACTION_NONE },
    { "Exit", MENU_PAGE_NONE, MENU_ACTION_EXIT_TO_MONITOR }
};

static const menu_item_t s_page_msg_groups_items[] =
{
    { "ALERT", MENU_PAGE_GROUP_ALERT, MENU_ACTION_NONE },
    { "STATUS", MENU_PAGE_GROUP_STATUS, MENU_ACTION_NONE },
    { "SERVICE", MENU_PAGE_GROUP_SERVICE, MENU_ACTION_NONE },
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

static const menu_item_t s_page_main_items[] =
{
    { "Devices", MENU_PAGE_DEVICES, MENU_ACTION_NONE },
    { "Security", MENU_PAGE_SECURITY, MENU_ACTION_NONE },
    { "Hardware", MENU_PAGE_HARDWARE, MENU_ACTION_NONE },
    { "Modulation", MENU_PAGE_MODULATION, MENU_ACTION_NONE },
    { "Info", MENU_PAGE_INFO, MENU_ACTION_NONE },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_devices_items[] =
{
    { "Add new device", MENU_PAGE_NONE, MENU_ACTION_DEVICE_ADD },
    { "Delete device", MENU_PAGE_NONE, MENU_ACTION_DEVICE_DELETE },
    { "Info device", MENU_PAGE_NONE, MENU_ACTION_DEVICE_INFO },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_security_items[] =
{
    { "Frequency hopping", MENU_PAGE_NONE, MENU_ACTION_SEC_TOGGLE_FH },
    { "TPM", MENU_PAGE_NONE, MENU_ACTION_SEC_TPM_INFO },
    { "Keys", MENU_PAGE_NONE, MENU_ACTION_SEC_ROTATE_KEYS },
    { "Coding", MENU_PAGE_NONE, MENU_ACTION_SEC_TOGGLE_CODING },
    { "Notif mode", MENU_PAGE_NONE, MENU_ACTION_SEC_TOGGLE_NOTIFY_MODE },
    { "Auto ping", MENU_PAGE_NONE, MENU_ACTION_SEC_TOGGLE_AUTOPING },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_hardware_items[] =
{
    { "Dist measure", MENU_PAGE_NONE, MENU_ACTION_HW_MEASURE_DIST },
    { "Temperature", MENU_PAGE_NONE, MENU_ACTION_HW_MEASURE_TEMP },
    { "Pressure", MENU_PAGE_NONE, MENU_ACTION_HW_MEASURE_PRESS },
    { "Led", MENU_PAGE_NONE, MENU_ACTION_HW_LED_MODE },
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
    { "MSG GROUPS", MENU_PAGE_PAGER, s_page_msg_groups_items, (uint8_t)(sizeof(s_page_msg_groups_items) / sizeof(s_page_msg_groups_items[0])) },
    { "ALERT", MENU_PAGE_MSG_GROUPS, s_page_group_alert_items, (uint8_t)(sizeof(s_page_group_alert_items) / sizeof(s_page_group_alert_items[0])) },
    { "STATUS", MENU_PAGE_MSG_GROUPS, s_page_group_status_items, (uint8_t)(sizeof(s_page_group_status_items) / sizeof(s_page_group_status_items[0])) },
    { "SERVICE", MENU_PAGE_MSG_GROUPS, s_page_group_service_items, (uint8_t)(sizeof(s_page_group_service_items) / sizeof(s_page_group_service_items[0])) },
    { "MAIN MENU", MENU_PAGE_PAGER, s_page_main_items, (uint8_t)(sizeof(s_page_main_items) / sizeof(s_page_main_items[0])) },
    { "DEVICES", MENU_PAGE_MAIN, s_page_devices_items, (uint8_t)(sizeof(s_page_devices_items) / sizeof(s_page_devices_items[0])) },
    { "SECURITY", MENU_PAGE_MAIN, s_page_security_items, (uint8_t)(sizeof(s_page_security_items) / sizeof(s_page_security_items[0])) },
    { "HARDWARE", MENU_PAGE_MAIN, s_page_hardware_items, (uint8_t)(sizeof(s_page_hardware_items) / sizeof(s_page_hardware_items[0])) },
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
            printf("MENU: notify queue create failed\r\n");
            return;
        }
    }

    if (s_menu_task == NULL)
    {
        s_menu_task = osThreadNew(menu_main_task_fn, NULL, &s_menu_task_attr);
        if (s_menu_task == NULL)
        {
            printf("MENU: task create failed\r\n");
        }
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
    st.current_page = MENU_PAGE_NONE;
    st.popup_enabled = true;
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

        if (s_menu_notify_queue != NULL)
        {
            if (osMessageQueueGet(s_menu_notify_queue, &notify, NULL, 0U) == osOK)
            {
                menu_handle_notification(&st, &notify);
            }
        }

        if ((st.current_page != MENU_PAGE_NONE) && (!st.popup_active))
        {
            if ((HAL_GetTick() - st.last_input_ms) >= MENU_INACTIVITY_TIMEOUT_MS)
            {
                menu_enter_monitor(&st);
            }
        }
    }
}

static void menu_enter_monitor(menu_state_t *st)
{
    if (st == NULL)
    {
        return;
    }

    st->current_page = MENU_PAGE_NONE;
    st->selected_idx = 0U;
    st->popup_active = false;
    st->pairing_prompt = false;
    st->pair_setup_prompt = false;
    st->send_prompt = false;
    st->pending_send_action = MENU_ACTION_NONE;
    (void)lcd_main_set_mode(LCD_MODE_MONITOR);
}

static void menu_open_page(menu_state_t *st, menu_page_id_t page_id)
{
    if (st == NULL)
    {
        return;
    }

    st->current_page = page_id;
    st->selected_idx = 0U;
    st->popup_active = false;
    st->pairing_prompt = false;
    st->pair_setup_prompt = false;
    st->send_prompt = false;
    st->pending_send_action = MENU_ACTION_NONE;
    (void)lcd_main_set_mode(LCD_MODE_MENU);
    menu_render(st);
}

static void menu_render(menu_state_t *st)
{
    char line[21];
    uint8_t row;
    uint8_t start_idx;
    const menu_page_t *page;

    if (st == NULL)
    {
        return;
    }

    page = menu_get_page(st->current_page);
    if (page == NULL)
    {
        return;
    }

    (void)lcd_main_set_mode(LCD_MODE_MENU);
    (void)lcd_main_set_line(0U, page->title);

    start_idx = (uint8_t)((st->selected_idx / MENU_ITEMS_VISIBLE) * MENU_ITEMS_VISIBLE);
    for (row = 0U; row < MENU_ITEMS_VISIBLE; row++)
    {
        uint8_t item_idx = (uint8_t)(start_idx + row);

        memset(line, 0, sizeof(line));
        if (item_idx < page->item_count)
        {
            snprintf(line,
                     sizeof(line),
                     "%c%-19s",
                     (item_idx == st->selected_idx) ? '>' : ' ',
                     page->items[item_idx].label);
            line[20] = '\0';
            (void)lcd_main_set_line((uint8_t)(row + 1U), line);
        }
        else
        {
            (void)lcd_main_set_line((uint8_t)(row + 1U), "");
        }
    }
}

static void menu_render_popup(const char *l0, const char *l1, const char *l2, const char *l3)
{
    (void)lcd_main_show_popup(l0, l1, l2, l3);
}

static void menu_handle_notification(menu_state_t *st, const menu_notification_t *n)
{
    bool has_text;
    bool force_popup;

    if ((st == NULL) || (n == NULL))
    {
        return;
    }

    if (st->current_page == MENU_PAGE_NONE)
    {
        return;
    }

    if (st->pairing_prompt || st->pair_setup_prompt || st->send_prompt)
    {
        return;
    }

    has_text = (n->text[0] != '\0');
    force_popup = ((n->type == MENU_NOTIFICATION_RX) || (n->type == MENU_NOTIFICATION_PAIRING));

    /*
     * Zwykłe komunikaty typu NOTICE/WARNING/SECURITY potrafiły zasłaniać ekran
     * konfiguracji w trakcie poruszania się po menu. W UI menu pokazujemy tylko
     * powiadomienia krytyczne dla interakcji radiowej: RX oraz pairing.
     */
    if (!force_popup)
    {
        return;
    }

    if (!st->popup_enabled && !force_popup)
    {
        return;
    }
    if (!has_text && (n->type != MENU_NOTIFICATION_PAIRING))
    {
        return;
    }

    {
        char rx_line0[21];
        char rx_line1[21];
        char code_line[21];
        char text_safe[21];
        const char *title = "NOTICE";

        memcpy(text_safe, n->text, sizeof(text_safe));
        text_safe[sizeof(text_safe) - 1U] = '\0';

        switch (n->type)
        {
            case MENU_NOTIFICATION_RX:
                title = "RX";
                break;
            case MENU_NOTIFICATION_WARNING:
                title = "WARNING";
                break;
            case MENU_NOTIFICATION_ERROR:
                title = "ERROR";
                break;
            case MENU_NOTIFICATION_PAIRING:
                title = "PAIRING";
                break;
            case MENU_NOTIFICATION_SECURITY:
                title = "SECURITY";
                break;
            default:
                break;
        }

        st->popup_active = true;
        st->pairing_prompt = false;
        st->pair_setup_prompt = false;
        st->send_prompt = false;
        st->pending_send_action = MENU_ACTION_NONE;
        st->page_before_popup = st->current_page;

        if (n->type == MENU_NOTIFICATION_RX)
        {
            memset(rx_line0, 0, sizeof(rx_line0));
            memset(rx_line1, 0, sizeof(rx_line1));
            snprintf(rx_line0, sizeof(rx_line0), "RX: %.16s", has_text ? text_safe : "");
            snprintf(rx_line1, sizeof(rx_line1), "RSSI: %d dBm", (int)n->rssi_dbm);
            menu_render_popup(rx_line0, rx_line1, "", "");
        }
        else if ((n->type == MENU_NOTIFICATION_PAIRING) &&
            (strncmp(text_safe, "JOIN_REQ", 8U) == 0))
        {
            st->pairing_prompt = true;
            snprintf(code_line, sizeof(code_line), "Code: %.11s", (text_safe[8] == ' ') ? &text_safe[9] : "----");
            menu_render_popup("PAIR REQUEST", code_line, "OK=accept", "Hold OK=reject");
        }
        else if ((n->type == MENU_NOTIFICATION_PAIRING) &&
                 (strncmp(text_safe, "JOIN_SENT", 9U) == 0))
        {
            snprintf(code_line, sizeof(code_line), "Code: %.11s", (text_safe[9] == ' ') ? &text_safe[10] : "----");
            menu_render_popup("JOIN REQ SENT", code_line, "Wait for JOIN_OK", "");
        }
        else if ((n->type == MENU_NOTIFICATION_PAIRING) &&
                 (strncmp(text_safe, "JOIN_OK", 7U) == 0))
        {
            snprintf(code_line, sizeof(code_line), "Code: %.11s", (text_safe[7] == ' ') ? &text_safe[8] : "----");
            menu_render_popup("PAIRING OK", code_line, "Device trusted", "");
        }
        else
        {
            menu_render_popup(title, has_text ? text_safe : "(empty)", "", "");
        }
    }
}

static void menu_handle_button(menu_state_t *st, button_event_t evt)
{
    const menu_page_t *page;

    if (st == NULL)
    {
        return;
    }

    if (st->popup_active)
    {
        if (st->pairing_prompt)
        {
            if (evt == BUTTON_EVENT_OK_SHORT)
            {
                (void)radio_main_cmd_pairing_accept(true);
            }
            else if (evt == BUTTON_EVENT_OK_LONG)
            {
                (void)radio_main_cmd_pairing_accept(false);
            }
            else
            {
                return;
            }
            st->pairing_prompt = false;
        }
        else if (st->pair_setup_prompt)
        {
            bool ok = false;
            bool send_join_req = false;

            st->pair_setup_prompt = false;
            st->popup_active = false;
            st->current_page = st->page_before_popup;

            if (evt == BUTTON_EVENT_OK_SHORT)
            {
                send_join_req = false;
            }
            else if (evt == BUTTON_EVENT_OK_LONG)
            {
                send_join_req = true;
            }
            else
            {
                menu_render(st);
                return;
            }

            ok = menu_start_pairing_session(st, send_join_req);
            if (!ok)
            {
                menu_notify_text(MENU_NOTIFICATION_ERROR, "Pairing start failed");
                menu_render(st);
            }
            return;
        }
        else if (st->send_prompt)
        {
            menu_action_t action = st->pending_send_action;
            st->send_prompt = false;
            st->pending_send_action = MENU_ACTION_NONE;
            st->popup_active = false;
            st->current_page = st->page_before_popup;

            if (evt == BUTTON_EVENT_OK_SHORT)
            {
                menu_execute_action(st, action);
            }
            else
            {
                menu_render(st);
            }
            return;
        }

        st->popup_active = false;
        st->current_page = st->page_before_popup;
        menu_render(st);
        return;
    }

    if (st->current_page == MENU_PAGE_NONE)
    {
        if (evt != BUTTON_EVENT_NONE)
        {
            menu_open_page(st, MENU_PAGE_PAGER);
        }
        return;
    }

    page = menu_get_page(st->current_page);
    if (page == NULL)
    {
        menu_enter_monitor(st);
        return;
    }

    switch (evt)
    {
        case BUTTON_EVENT_UP_SHORT:
            if (page->item_count > 0U)
            {
                if (st->selected_idx == 0U)
                {
                    st->selected_idx = (uint8_t)(page->item_count - 1U);
                }
                else
                {
                    st->selected_idx--;
                }
                menu_render(st);
            }
            break;

        case BUTTON_EVENT_DOWN_SHORT:
            if (page->item_count > 0U)
            {
                st->selected_idx++;
                if (st->selected_idx >= page->item_count)
                {
                    st->selected_idx = 0U;
                }
                menu_render(st);
            }
            break;

        case BUTTON_EVENT_OK_SHORT:
            if (st->selected_idx < page->item_count)
            {
                const menu_item_t *item = &page->items[st->selected_idx];
                if (item->child_page != MENU_PAGE_NONE)
                {
                    menu_open_page(st, item->child_page);
                }
                else if (menu_is_send_action(item->action))
                {
                    menu_open_send_prompt(st, item->action, item->label);
                }
                else
                {
                    menu_execute_action(st, item->action);
                }
            }
            break;

        case BUTTON_EVENT_OK_LONG:
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
    char line0[21];
    char line1[21];
    security_runtime_cfg_t cfg;
    trusted_info_t info;
    bool tpm_ready;
    bmp280_api_data_t bmp;
    int32_t distance_mm;
    uint8_t idx;
    uint8_t count;
    bool send_ok;

    memset(line0, 0, sizeof(line0));
    memset(line1, 0, sizeof(line1));

    if (menu_execute_radio_action(action))
    {
        return;
    }

    switch (action)
    {
        case MENU_ACTION_BACK:
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
            break;
        }

        case MENU_ACTION_EXIT_TO_MONITOR:
            menu_enter_monitor(st);
            break;

        case MENU_ACTION_SEND_DEFAULT:
            send_ok = radio_main_cmd_send_template(1U, 0U, BEKO_NET_BROADCAST_ID);
            menu_notify_text(send_ok ? MENU_NOTIFICATION_SECURITY : MENU_NOTIFICATION_ERROR,
                             send_ok ? "Sent STS:OK" : "Send failed");
            break;

        case MENU_ACTION_SEND_ALERT_FIRE:
            send_ok = radio_main_cmd_send_template(0U, 0U, BEKO_NET_BROADCAST_ID);
            menu_notify_text(send_ok ? MENU_NOTIFICATION_SECURITY : MENU_NOTIFICATION_ERROR,
                             send_ok ? "Sent ALR:FIRE" : "Send failed");
            break;
        case MENU_ACTION_SEND_ALERT_INTR:
            send_ok = radio_main_cmd_send_template(0U, 1U, BEKO_NET_BROADCAST_ID);
            menu_notify_text(send_ok ? MENU_NOTIFICATION_SECURITY : MENU_NOTIFICATION_ERROR,
                             send_ok ? "Sent ALR:INTR" : "Send failed");
            break;
        case MENU_ACTION_SEND_ALERT_LOWBATT:
            send_ok = radio_main_cmd_send_template(0U, 2U, BEKO_NET_BROADCAST_ID);
            menu_notify_text(send_ok ? MENU_NOTIFICATION_SECURITY : MENU_NOTIFICATION_ERROR,
                             send_ok ? "Sent ALR:LOW" : "Send failed");
            break;

        case MENU_ACTION_SEND_STATUS_OK:
            send_ok = radio_main_cmd_send_template(1U, 0U, BEKO_NET_BROADCAST_ID);
            menu_notify_text(send_ok ? MENU_NOTIFICATION_SECURITY : MENU_NOTIFICATION_ERROR,
                             send_ok ? "Sent STS:OK" : "Send failed");
            break;
        case MENU_ACTION_SEND_STATUS_BUSY:
            send_ok = radio_main_cmd_send_template(1U, 1U, BEKO_NET_BROADCAST_ID);
            menu_notify_text(send_ok ? MENU_NOTIFICATION_SECURITY : MENU_NOTIFICATION_ERROR,
                             send_ok ? "Sent STS:BUSY" : "Send failed");
            break;
        case MENU_ACTION_SEND_STATUS_IDLE:
            send_ok = radio_main_cmd_send_template(1U, 2U, BEKO_NET_BROADCAST_ID);
            menu_notify_text(send_ok ? MENU_NOTIFICATION_SECURITY : MENU_NOTIFICATION_ERROR,
                             send_ok ? "Sent STS:IDLE" : "Send failed");
            break;

        case MENU_ACTION_SEND_SERVICE_PING:
            send_ok = radio_main_cmd_send_template(2U, 0U, BEKO_NET_BROADCAST_ID);
            menu_notify_text(send_ok ? MENU_NOTIFICATION_SECURITY : MENU_NOTIFICATION_ERROR,
                             send_ok ? "Sent SRV:PING" : "Send failed");
            break;
        case MENU_ACTION_SEND_SERVICE_RESET:
            send_ok = radio_main_cmd_send_template(2U, 1U, BEKO_NET_BROADCAST_ID);
            menu_notify_text(send_ok ? MENU_NOTIFICATION_SECURITY : MENU_NOTIFICATION_ERROR,
                             send_ok ? "Sent SRV:RESET" : "Send failed");
            break;
        case MENU_ACTION_SEND_SERVICE_SYNC:
            send_ok = radio_main_cmd_send_template(2U, 2U, BEKO_NET_BROADCAST_ID);
            menu_notify_text(send_ok ? MENU_NOTIFICATION_SECURITY : MENU_NOTIFICATION_ERROR,
                             send_ok ? "Sent SRV:SYNC" : "Send failed");
            break;

        case MENU_ACTION_DEVICE_ADD:
            menu_render_popup("PAIR MODE 60s", "OK=listen", "Hold OK=JOIN_REQ", "Any=cancel");
            st->popup_active = true;
            st->pairing_prompt = false;
            st->pair_setup_prompt = true;
            st->send_prompt = false;
            st->pending_send_action = MENU_ACTION_NONE;
            st->page_before_popup = st->current_page;
            break;

        case MENU_ACTION_DEVICE_DELETE:
            for (idx = 0U; idx < 16U; idx++)
            {
                if (security_main_cmd_get_device(idx, &info) && info.in_use)
                {
                    bool deleted = security_main_cmd_delete_device(info.node_id);
                    bool notified = false;

                    if (deleted)
                    {
                        notified = radio_main_cmd_send_trust_removed(info.node_id);
                    }

                    if (!deleted)
                    {
                        menu_notify_text(MENU_NOTIFICATION_ERROR, "Delete failed");
                    }
                    else if (notified)
                    {
                        snprintf(line0, sizeof(line0), "Deleted slot %u", idx);
                        menu_notify_text(MENU_NOTIFICATION_SECURITY, line0);
                    }
                    else
                    {
                        menu_notify_text(MENU_NOTIFICATION_WARNING, "Deleted local only");
                    }
                    return;
                }
            }
            menu_notify_text(MENU_NOTIFICATION_WARNING, "No device");
            break;

        case MENU_ACTION_DEVICE_INFO:
            count = 0U;
            for (idx = 0U; idx < 16U; idx++)
            {
                if (security_main_cmd_get_device(idx, &info) && info.in_use)
                {
                    count++;
                }
            }
            snprintf(line0, sizeof(line0), "Trusted count=%u", count);
            menu_render_popup("DEVICE INFO", line0, "", "");
            st->popup_active = true;
            st->pairing_prompt = false;
            st->pair_setup_prompt = false;
            st->page_before_popup = st->current_page;
            break;

        case MENU_ACTION_SEC_TOGGLE_FH:
            if (security_main_cmd_get_runtime_cfg(&cfg))
            {
                bool new_state = !cfg.fh_enabled;
                (void)security_main_cmd_set_fh(new_state);
                (void)radio_main_cmd_set_fh(new_state);
                menu_notify_text(MENU_NOTIFICATION_SECURITY, new_state ? "FH ON" : "FH OFF");
            }
            break;

        case MENU_ACTION_SEC_TPM_INFO:
            if (security_main_get_tpm_ready(&tpm_ready))
            {
                menu_render_popup("TPM", tpm_ready ? "Ready" : "Not ready", "", "");
                st->popup_active = true;
                st->pairing_prompt = false;
                st->pair_setup_prompt = false;
                st->page_before_popup = st->current_page;
            }
            break;

        case MENU_ACTION_SEC_ROTATE_KEYS:
            if (security_main_cmd_rotate_key())
            {
                menu_notify_text(MENU_NOTIFICATION_SECURITY, "Key rotated");
            }
            else
            {
                menu_notify_text(MENU_NOTIFICATION_ERROR, "Key rotate failed");
            }
            break;

        case MENU_ACTION_SEC_TOGGLE_CODING:
            if (security_main_cmd_get_runtime_cfg(&cfg))
            {
                bool new_state = !cfg.coding_enabled;
                (void)security_main_cmd_set_coding(new_state);
                (void)radio_main_cmd_set_coding(new_state);
                menu_notify_text(MENU_NOTIFICATION_SECURITY, new_state ? "Coding ON" : "Coding OFF");
            }
            break;

        case MENU_ACTION_SEC_TOGGLE_NOTIFY_MODE:
            if (security_main_cmd_get_runtime_cfg(&cfg))
            {
                security_notify_mode_t mode = (cfg.notify_mode == SECURITY_NOTIFY_POPUP) ?
                                              SECURITY_NOTIFY_BADGE : SECURITY_NOTIFY_POPUP;
                (void)security_main_cmd_set_notify_mode(mode);
                st->popup_enabled = (mode == SECURITY_NOTIFY_POPUP);
                menu_notify_text(MENU_NOTIFICATION_SECURITY, st->popup_enabled ? "Notif POPUP" : "Notif BADGE");
            }
            break;

        case MENU_ACTION_SEC_TOGGLE_AUTOPING:
            if (security_main_cmd_get_runtime_cfg(&cfg))
            {
                bool new_state = !cfg.auto_ping_enabled;
                (void)security_main_cmd_set_auto_ping(new_state);
                (void)radio_main_cmd_set_auto_ping(new_state);
                menu_notify_text(MENU_NOTIFICATION_SECURITY, new_state ? "AutoPing ON" : "AutoPing OFF");
            }
            break;

        case MENU_ACTION_HW_MEASURE_DIST:
            distance_mm = tof_main_get_last_distance();
            snprintf(line0, sizeof(line0), "Distance");
            snprintf(line1, sizeof(line1), "%ld mm", (long)distance_mm);
            menu_render_popup(line0, line1, "", "");
            st->popup_active = true;
            st->pairing_prompt = false;
            st->pair_setup_prompt = false;
            st->page_before_popup = st->current_page;
            break;

        case MENU_ACTION_HW_MEASURE_TEMP:
            if (bmp280_main_get_last(&bmp))
            {
                snprintf(line0, sizeof(line0), "Temp %.2f C", bmp.temperature_c);
                menu_render_popup("BMP280", line0, "", "");
            }
            else
            {
                menu_render_popup("BMP280", "No data", "", "");
            }
            st->popup_active = true;
            st->pairing_prompt = false;
            st->pair_setup_prompt = false;
            st->page_before_popup = st->current_page;
            break;

        case MENU_ACTION_HW_MEASURE_PRESS:
            if (bmp280_main_get_last(&bmp))
            {
                snprintf(line0, sizeof(line0), "Press %.2f hPa", bmp.pressure_hpa);
                menu_render_popup("BMP280", line0, "", "");
            }
            else
            {
                menu_render_popup("BMP280", "No data", "", "");
            }
            st->popup_active = true;
            st->pairing_prompt = false;
            st->pair_setup_prompt = false;
            st->page_before_popup = st->current_page;
            break;

        case MENU_ACTION_HW_LED_MODE:
            st->led_mode = (uint8_t)((st->led_mode + 1U) % 3U);
            if (st->led_mode == 0U)
            {
                (void)led_array_start_rainbow(15U, 5U, 100U);
                menu_notify_text(MENU_NOTIFICATION_SECURITY, "LED rainbow");
            }
            else if (st->led_mode == 1U)
            {
                (void)led_array_start_breath(LED_ARRAY_LED_ALL, 1200U, 5U, 100U);
                menu_notify_text(MENU_NOTIFICATION_SECURITY, "LED breath");
            }
            else
            {
                (void)led_array_stop_effect();
                (void)led_array_off(LED_ARRAY_LED_ALL);
                menu_notify_text(MENU_NOTIFICATION_SECURITY, "LED off");
            }
            break;

        case MENU_ACTION_MOD_LORA_STD:
            (void)radio_main_cmd_set_modulation(0U);
            (void)radio_main_cmd_set_lora_preset(0U);
            (void)security_main_cmd_set_lora_preset(0U);
            menu_notify_text(MENU_NOTIFICATION_SECURITY, "LoRa STD");
            break;
        case MENU_ACTION_MOD_LORA_RANGE:
            (void)radio_main_cmd_set_modulation(0U);
            (void)radio_main_cmd_set_lora_preset(1U);
            (void)security_main_cmd_set_lora_preset(1U);
            menu_notify_text(MENU_NOTIFICATION_SECURITY, "LoRa RANGE");
            break;
        case MENU_ACTION_MOD_LORA_FAST:
            (void)radio_main_cmd_set_modulation(0U);
            (void)radio_main_cmd_set_lora_preset(2U);
            (void)security_main_cmd_set_lora_preset(2U);
            menu_notify_text(MENU_NOTIFICATION_SECURITY, "LoRa FAST");
            break;

        case MENU_ACTION_MOD_FSK_ENABLE:
            if (radio_main_cmd_set_modulation(1U))
            {
                menu_notify_text(MENU_NOTIFICATION_SECURITY, "FSK mode");
            }
            else
            {
                menu_notify_text(MENU_NOTIFICATION_ERROR, "FSK set failed");
            }
            break;
        case MENU_ACTION_MOD_FSK_FREQ_8681:
            (void)radio_main_cmd_set_modulation(1U);
            if (radio_main_cmd_set_modulation_freq(868100000UL))
            {
                menu_notify_text(MENU_NOTIFICATION_SECURITY, "FSK F=868.1");
            }
            else
            {
                menu_notify_text(MENU_NOTIFICATION_ERROR, "FSK freq failed");
            }
            break;
        case MENU_ACTION_MOD_FSK_FREQ_8683:
            (void)radio_main_cmd_set_modulation(1U);
            if (radio_main_cmd_set_modulation_freq(868300000UL))
            {
                menu_notify_text(MENU_NOTIFICATION_SECURITY, "FSK F=868.3");
            }
            else
            {
                menu_notify_text(MENU_NOTIFICATION_ERROR, "FSK freq failed");
            }
            break;
        case MENU_ACTION_MOD_FSK_FREQ_8685:
            (void)radio_main_cmd_set_modulation(1U);
            if (radio_main_cmd_set_modulation_freq(868500000UL))
            {
                menu_notify_text(MENU_NOTIFICATION_SECURITY, "FSK F=868.5");
            }
            else
            {
                menu_notify_text(MENU_NOTIFICATION_ERROR, "FSK freq failed");
            }
            break;
        case MENU_ACTION_MOD_FSK_BW_125:
            (void)radio_main_cmd_set_modulation(1U);
            if (radio_main_cmd_set_modulation_bw((uint8_t)RADIO_LORA_BW_125_KHZ))
            {
                menu_notify_text(MENU_NOTIFICATION_SECURITY, "FSK BW125");
            }
            else
            {
                menu_notify_text(MENU_NOTIFICATION_ERROR, "FSK bw failed");
            }
            break;
        case MENU_ACTION_MOD_FSK_BW_250:
            (void)radio_main_cmd_set_modulation(1U);
            if (radio_main_cmd_set_modulation_bw((uint8_t)RADIO_LORA_BW_250_KHZ))
            {
                menu_notify_text(MENU_NOTIFICATION_SECURITY, "FSK BW250");
            }
            else
            {
                menu_notify_text(MENU_NOTIFICATION_ERROR, "FSK bw failed");
            }
            break;
        case MENU_ACTION_MOD_FSK_BW_500:
            (void)radio_main_cmd_set_modulation(1U);
            if (radio_main_cmd_set_modulation_bw((uint8_t)RADIO_LORA_BW_500_KHZ))
            {
                menu_notify_text(MENU_NOTIFICATION_SECURITY, "FSK BW500");
            }
            else
            {
                menu_notify_text(MENU_NOTIFICATION_ERROR, "FSK bw failed");
            }
            break;

        case MENU_ACTION_MOD_OOK_ENABLE:
            if (radio_main_cmd_set_modulation(2U))
            {
                menu_notify_text(MENU_NOTIFICATION_SECURITY, "OOK mode");
            }
            else
            {
                menu_notify_text(MENU_NOTIFICATION_ERROR, "OOK set failed");
            }
            break;
        case MENU_ACTION_MOD_OOK_FREQ_8681:
            (void)radio_main_cmd_set_modulation(2U);
            if (radio_main_cmd_set_modulation_freq(868100000UL))
            {
                menu_notify_text(MENU_NOTIFICATION_SECURITY, "OOK F=868.1");
            }
            else
            {
                menu_notify_text(MENU_NOTIFICATION_ERROR, "OOK freq failed");
            }
            break;
        case MENU_ACTION_MOD_OOK_FREQ_8683:
            (void)radio_main_cmd_set_modulation(2U);
            if (radio_main_cmd_set_modulation_freq(868300000UL))
            {
                menu_notify_text(MENU_NOTIFICATION_SECURITY, "OOK F=868.3");
            }
            else
            {
                menu_notify_text(MENU_NOTIFICATION_ERROR, "OOK freq failed");
            }
            break;
        case MENU_ACTION_MOD_OOK_FREQ_8685:
            (void)radio_main_cmd_set_modulation(2U);
            if (radio_main_cmd_set_modulation_freq(868500000UL))
            {
                menu_notify_text(MENU_NOTIFICATION_SECURITY, "OOK F=868.5");
            }
            else
            {
                menu_notify_text(MENU_NOTIFICATION_ERROR, "OOK freq failed");
            }
            break;
        case MENU_ACTION_MOD_OOK_BW_125:
            (void)radio_main_cmd_set_modulation(2U);
            if (radio_main_cmd_set_modulation_bw((uint8_t)RADIO_LORA_BW_125_KHZ))
            {
                menu_notify_text(MENU_NOTIFICATION_SECURITY, "OOK BW125");
            }
            else
            {
                menu_notify_text(MENU_NOTIFICATION_ERROR, "OOK bw failed");
            }
            break;
        case MENU_ACTION_MOD_OOK_BW_250:
            (void)radio_main_cmd_set_modulation(2U);
            if (radio_main_cmd_set_modulation_bw((uint8_t)RADIO_LORA_BW_250_KHZ))
            {
                menu_notify_text(MENU_NOTIFICATION_SECURITY, "OOK BW250");
            }
            else
            {
                menu_notify_text(MENU_NOTIFICATION_ERROR, "OOK bw failed");
            }
            break;
        case MENU_ACTION_MOD_OOK_BW_500:
            (void)radio_main_cmd_set_modulation(2U);
            if (radio_main_cmd_set_modulation_bw((uint8_t)RADIO_LORA_BW_500_KHZ))
            {
                menu_notify_text(MENU_NOTIFICATION_SECURITY, "OOK BW500");
            }
            else
            {
                menu_notify_text(MENU_NOTIFICATION_ERROR, "OOK bw failed");
            }
            break;

        case MENU_ACTION_MOD_FSK:
            (void)radio_main_cmd_set_modulation(1U);
            menu_notify_text(MENU_NOTIFICATION_SECURITY, "FSK mode");
            break;
        case MENU_ACTION_MOD_OOK:
            (void)radio_main_cmd_set_modulation(2U);
            menu_notify_text(MENU_NOTIFICATION_SECURITY, "OOK mode");
            break;

        case MENU_ACTION_INFO_SHOW:
            snprintf(line0, sizeof(line0), "Node 0x%08lX", (unsigned long)radio_main_get_node_id());
            menu_render_popup("BEKO W1", line0, "Codex build", "Menu/Sec active");
            st->popup_active = true;
            st->pairing_prompt = false;
            st->pair_setup_prompt = false;
            st->page_before_popup = st->current_page;
            break;

        default:
            break;
    }
}

/**
 * @brief Obsługuje całą rodzinę akcji konfiguracji radiowej przez tablicę wiązań.
 *
 * Dzięki temu dodanie nowej opcji w menu sprowadza się do:
 * 1. dodania pozycji do strony LCD,
 * 2. dopisania jednej linijki w `s_radio_action_bindings`.
 *
 * Sama logika aplikacji nie jest już powielana w dużym `switch`.
 *
 * @param action Akcja wybrana z menu.
 * @return `true`, jeśli akcja została rozpoznana i obsłużona.
 */
static bool menu_execute_radio_action(menu_action_t action)
{
    size_t idx;

    for (idx = 0U; idx < (sizeof(s_radio_action_bindings) / sizeof(s_radio_action_bindings[0])); idx++)
    {
        const menu_radio_action_binding_t *binding = &s_radio_action_bindings[idx];
        radio_main_runtime_cfg_t runtime_cfg;
        bool have_runtime = false;
        bool ok = true;

        if (binding->action != action)
        {
            continue;
        }

        have_runtime = radio_main_get_runtime_cfg(&runtime_cfg);
        if (binding->set_modulation)
        {
            if (!have_runtime || (runtime_cfg.active_modulation != binding->modulation))
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

        menu_notify_text(ok ? MENU_NOTIFICATION_SECURITY : MENU_NOTIFICATION_ERROR,
                         ok ? binding->ok_text : binding->err_text);
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
            return true;
        default:
            return false;
    }
}

static void menu_open_send_prompt(menu_state_t *st, menu_action_t action, const char *label)
{
    char msg[21];

    if (st == NULL)
    {
        return;
    }

    snprintf(msg, sizeof(msg), "%s", (label != NULL) ? label : "Message");
    st->popup_active = true;
    st->pairing_prompt = false;
    st->pair_setup_prompt = false;
    st->send_prompt = true;
    st->pending_send_action = action;
    st->page_before_popup = st->current_page;
    menu_render_popup("SEND MESSAGE?", msg, "OK=send", "Hold OK=back");
}

static bool menu_start_pairing_session(menu_state_t *st, bool send_join_req)
{
    bool send_ok = true;

    if (st == NULL)
    {
        return false;
    }

    if (!radio_main_cmd_start_pairing(60000U))
    {
        return false;
    }

    if (send_join_req)
    {
        send_ok = radio_main_cmd_send_join_req();
    }

    st->popup_active = true;
    st->pairing_prompt = false;
    st->pair_setup_prompt = false;
    st->send_prompt = false;
    st->pending_send_action = MENU_ACTION_NONE;
    st->page_before_popup = st->current_page;

    if (send_join_req)
    {
        menu_render_popup("PAIR MODE", "60s active", send_ok ? "JOIN_REQ sent" : "JOIN_REQ failed", "Any key=close");
    }
    else
    {
        menu_render_popup("PAIR MODE", "60s active", "Listening...", "Any key=close");
    }

    return true;
}

static void menu_notify_text(menu_notification_type_t type, const char *text)
{
    menu_notification_t n;

    memset(&n, 0, sizeof(n));
    n.type = type;
    if (text != NULL)
    {
        snprintf(n.text, sizeof(n.text), "%s", text);
    }
    (void)menu_main_post_notification(&n);
}
