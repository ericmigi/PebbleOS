/*
 * SPDX-FileCopyrightText: Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 *
 * The ESP32-C3 ROM symbols the Bluetooth libraries use, from ESP-IDF 5.5.5
 * components/esp_rom/esp32c3/ld, written by gen_rom_ld.py: do not edit.
 */

/* esp32c3.rom.bt_funcs.ld */
PROVIDE(bt_rf_coex_get_dft_cfg = 0x400008dc);
PROVIDE(r_bt_bb_isr = 0x40000b9c);
PROVIDE(r_bt_rf_coex_conn_phy_coded_data_time_limit_en_get = 0x40000ba8);
PROVIDE(r_bt_rtp_get_txpwr_idx_by_act = 0x40000c00);
PROVIDE(r_btdm_task_post = 0x40000c14);
PROVIDE(r_btdm_task_post_from_isr = 0x40000c18);
PROVIDE(r_btdm_task_recycle = 0x40000c1c);
PROVIDE(r_hci_register_vendor_desc_tab = 0x40000d9c);
PROVIDE(r_ke_task_schedule = 0x40000e80);
PROVIDE(r_llc_con_upd_param_in_range = 0x40000ecc);
PROVIDE(r_llc_hci_command_handler = 0x40000ef0);
PROVIDE(r_llc_le_ping_restart = 0x40000f2c);
PROVIDE(r_llc_llcp_tx_check = 0x40000f58);
PROVIDE(r_llc_loc_ch_map_proc_continue = 0x40000f5c);
PROVIDE(r_llc_loc_con_upd_proc_continue = 0x40000f60);
PROVIDE(r_llc_loc_phy_upd_proc_continue = 0x40000f78);
PROVIDE(r_llc_rem_con_upd_proc_continue = 0x40000fb4);
PROVIDE(r_lld_adv_adv_data_update = 0x40000ff4);
PROVIDE(r_lld_adv_pkt_rx_send_scan_req_evt = 0x40001040);
PROVIDE(r_lld_adv_scan_rsp_data_update = 0x40001050);
PROVIDE(r_lld_con_evt_canceled_cbk = 0x400010b0);
PROVIDE(r_lld_con_rx = 0x400010fc);
PROVIDE(r_lld_con_rx_channel_assess = 0x40001100);
PROVIDE(r_lld_con_sched = 0x40001118);
PROVIDE(r_lld_con_stop = 0x40001124);
PROVIDE(r_lld_conn_dynamic_pti_process = 0x40001144);
PROVIDE(r_lld_llcp_rx_ind_handler = 0x400011b0);
PROVIDE(r_lld_per_adv_sched = 0x400011f8);
PROVIDE(r_lld_res_list_clear = 0x4000121c);
PROVIDE(r_lld_res_list_peer_update = 0x4000122c);
PROVIDE(r_lld_res_list_priv_mode_update = 0x40001230);
PROVIDE(r_lld_res_list_rem = 0x40001234);
PROVIDE(r_lld_scan_process_pkt_rx = 0x40001280);
PROVIDE(r_lld_scan_process_pkt_rx_adv_rep = 0x40001284);
PROVIDE(r_lld_test_stop = 0x40001334);
PROVIDE(r_llm_le_features_get = 0x400013b0);
PROVIDE(r_register_esp_vendor_cmd_handler = 0x40001400);
PROVIDE(r_rf_txpwr_cs_get = 0x40001428);
PROVIDE(r_rf_txpwr_dbm_get = 0x4000142c);
PROVIDE(r_rwble_isr = 0x40001464);
PROVIDE(r_rwbtdm_isr_wrapper = 0x40001470);
PROVIDE(r_rwip_assert = 0x4000147c);
PROVIDE(r_rwip_prevent_sleep_clear = 0x4000149c);
PROVIDE(r_rwip_prevent_sleep_set = 0x400014a0);
PROVIDE(r_rwip_wakeup_end = 0x400014d4);
PROVIDE(r_sch_arb_event_start_isr = 0x400014f8);
PROVIDE(r_sch_plan_set = 0x40001534);
PROVIDE(r_sch_prog_end_isr = 0x40001538);

/* esp32c3.rom.eco3_bt_funcs.ld */
PROVIDE(r_lld_adv_ext_chain_none_construct = 0x40001b50);
PROVIDE(r_lld_adv_ext_chain_scannable_construct = 0x40001b58);
PROVIDE(r_lld_adv_ext_pkt_prepare_set = 0x40001b4c);
PROVIDE(r_lld_adv_start_update_filter_policy = 0x40001b68);
PROVIDE(r_lld_con_tx_prog_new_packet = 0x40001b74);
PROVIDE(r_lld_scan_try_sched = 0x40001b84);
PROVIDE(r_sch_prog_ble_push = 0x40001b8c);

/* esp32c3.rom.ld */
PROVIDE(DebugE256PublicKey_x = 0x3ff1ff9c);
PROVIDE(DebugE256PublicKey_y = 0x3ff1ff7c);
PROVIDE(DebugE256SecretKey = 0x3ff1ff5c);
PROVIDE(TASK_DESC_LLC = 0x3fcdfa1c);
PROVIDE(TASK_DESC_LLM = 0x3fcdfa10);
PROVIDE(TASK_DESC_VSHCI = 0x3fcdfa04);
PROVIDE(aes_cmac_zero = 0x3ff1f750);
PROVIDE(aes_k2_salt = 0x3ff1f740);
PROVIDE(aes_k3_salt = 0x3ff1f728);
PROVIDE(aes_k4_salt = 0x3ff1f714);
PROVIDE(hci_cmd_desc_root_tab = 0x3ff1f4d8);
PROVIDE(r_modules_funcs_p = 0x3fcdff88);
