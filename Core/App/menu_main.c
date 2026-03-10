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
    MENU_PAGE_MOD_FSK,
    MENU_PAGE_MOD_FSK_FREQ,
    MENU_PAGE_MOD_FSK_BW,
    MENU_PAGE_MOD_OOK,
    MENU_PAGE_MOD_OOK_FREQ,
    MENU_PAGE_MOD_OOK_BW,
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
    MENU_ACTION_MOD_LORA_STD,
    MENU_ACTION_MOD_LORA_RANGE,
    MENU_ACTION_MOD_LORA_FAST,
    MENU_ACTION_MOD_FSK_ENABLE,
    MENU_ACTION_MOD_FSK_FREQ_8681,
    MENU_ACTION_MOD_FSK_FREQ_8683,
    MENU_ACTION_MOD_FSK_FREQ_8685,
    MENU_ACTION_MOD_FSK_BW_125,
    MENU_ACTION_MOD_FSK_BW_250,
    MENU_ACTION_MOD_FSK_BW_500,
    MENU_ACTION_MOD_OOK_ENABLE,
    MENU_ACTION_MOD_OOK_FREQ_8681,
    MENU_ACTION_MOD_OOK_FREQ_8683,
    MENU_ACTION_MOD_OOK_FREQ_8685,
    MENU_ACTION_MOD_OOK_BW_125,
    MENU_ACTION_MOD_OOK_BW_250,
    MENU_ACTION_MOD_OOK_BW_500,
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
    { "FSK", MENU_PAGE_MOD_FSK, MENU_ACTION_NONE },
    { "OOK", MENU_PAGE_MOD_OOK, MENU_ACTION_NONE },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_lora_items[] =
{
    { "STD 868.1", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_STD },
    { "RANGE 868.3", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_RANGE },
    { "FAST 868.5", MENU_PAGE_NONE, MENU_ACTION_MOD_LORA_FAST },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_fsk_items[] =
{
    { "Use FSK mode", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_ENABLE },
    { "Frequency", MENU_PAGE_MOD_FSK_FREQ, MENU_ACTION_NONE },
    { "Bandwidth", MENU_PAGE_MOD_FSK_BW, MENU_ACTION_NONE },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_fsk_freq_items[] =
{
    { "868.1 MHz", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_FREQ_8681 },
    { "868.3 MHz", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_FREQ_8683 },
    { "868.5 MHz", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_FREQ_8685 },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_fsk_bw_items[] =
{
    { "BW 125 kHz", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_BW_125 },
    { "BW 250 kHz", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_BW_250 },
    { "BW 500 kHz", MENU_PAGE_NONE, MENU_ACTION_MOD_FSK_BW_500 },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_ook_items[] =
{
    { "Use OOK mode", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_ENABLE },
    { "Frequency", MENU_PAGE_MOD_OOK_FREQ, MENU_ACTION_NONE },
    { "Bandwidth", MENU_PAGE_MOD_OOK_BW, MENU_ACTION_NONE },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_ook_freq_items[] =
{
    { "868.1 MHz", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_FREQ_8681 },
    { "868.3 MHz", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_FREQ_8683 },
    { "868.5 MHz", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_FREQ_8685 },
    { "Back", MENU_PAGE_NONE, MENU_ACTION_BACK }
};

static const menu_item_t s_page_mod_ook_bw_items[] =
{
    { "BW 125 kHz", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_BW_125 },
    { "BW 250 kHz", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_BW_250 },
    { "BW 500 kHz", MENU_PAGE_NONE, MENU_ACTION_MOD_OOK_BW_500 },
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
    { "FSK", MENU_PAGE_MODULATION, s_page_mod_fsk_items, (uint8_t)(sizeof(s_page_mod_fsk_items) / sizeof(s_page_mod_fsk_items[0])) },
    { "FSK FREQ", MENU_PAGE_MOD_FSK, s_page_mod_fsk_freq_items, (uint8_t)(sizeof(s_page_mod_fsk_freq_items) / sizeof(s_page_mod_fsk_freq_items[0])) },
    { "FSK BW", MENU_PAGE_MOD_FSK, s_page_mod_fsk_bw_items, (uint8_t)(sizeof(s_page_mod_fsk_bw_items) / sizeof(s_page_mod_fsk_bw_items[0])) },
    { "OOK", MENU_PAGE_MODULATION, s_page_mod_ook_items, (uint8_t)(sizeof(s_page_mod_ook_items) / sizeof(s_page_mod_ook_items[0])) },
    { "OOK FREQ", MENU_PAGE_MOD_OOK, s_page_mod_ook_freq_items, (uint8_t)(sizeof(s_page_mod_ook_freq_items) / sizeof(s_page_mod_ook_freq_items[0])) },
    { "OOK BW", MENU_PAGE_MOD_OOK, s_page_mod_ook_bw_items, (uint8_t)(sizeof(s_page_mod_ook_bw_items) / sizeof(s_page_mod_ook_bw_items[0])) },
    { "INFO", MENU_PAGE_MAIN, s_page_info_items, (uint8_t)(sizeof(s_page_info_items) / sizeof(s_page_info_items[0])) }
};

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
