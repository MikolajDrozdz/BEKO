#ifndef APP_RADIO_MAIN_H_
#define APP_RADIO_MAIN_H_

#include <stdbool.h>
#include <stdint.h>

void radio_main_create_task(void);

bool radio_main_cmd_send_template(uint8_t group_id, uint8_t msg_id, uint32_t dst_id);
bool radio_main_cmd_set_lora_preset(uint8_t preset_id);
bool radio_main_cmd_set_modulation(uint8_t modulation_id);
bool radio_main_cmd_set_fh(bool enabled);
bool radio_main_cmd_set_coding(bool enabled);
bool radio_main_cmd_set_auto_ping(bool enabled);

bool radio_main_cmd_start_pairing(uint32_t timeout_ms);
bool radio_main_cmd_pairing_accept(bool accept);
bool radio_main_cmd_send_join_req(void);
bool radio_main_cmd_send_trust_removed(uint32_t dst_id);

uint32_t radio_main_get_node_id(void);

#endif /* APP_RADIO_MAIN_H_ */
