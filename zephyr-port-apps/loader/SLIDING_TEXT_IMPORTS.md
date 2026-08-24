# Sliding Text emery SDK imports

The target PBW contains 41 generated app-side SDK trampolines at file offsets
0x7dc through 0x9bc. Each trampoline loads a byte offset into
g_pbl_system_tbl and branches to the common trampoline at PBW offset 0xa8.
The common trampoline reads the firmware table pointer from the loader-patched
slot at PBW offset 0xb8.

The mapping below was decoded from the Thumb mov/movw immediate in each
trampoline and checked against the current generated
build/src/fw/pebble.auto.c ordering.

| Table index | Byte offset | Firmware entry |
| ---: | ---: | --- |
| 31 | 124 | app_event_loop |
| 96 | 384 | fonts_get_system_font |
| 97 | 388 | fonts_load_custom_font |
| 98 | 392 | fonts_unload_custom_font |
| 99 | 396 | task_free |
| 138 | 552 | layer_add_child |
| 145 | 580 | layer_get_frame_by_value |
| 150 | 600 | layer_mark_dirty |
| 155 | 620 | layer_set_frame_by_value |
| 156 | 624 | layer_set_hidden |
| 161 | 644 | task_malloc |
| 206 | 824 | applib_resource_get_handle |
| 241 | 964 | strcat |
| 243 | 972 | strcpy |
| 245 | 980 | strlen |
| 262 | 1048 | tick_timer_service_subscribe |
| 263 | 1052 | tick_timer_service_unsubscribe |
| 271 | 1084 | window_create |
| 272 | 1088 | window_destroy |
| 275 | 1100 | window_get_root_layer |
| 287 | 1148 | app_window_stack_push |
| 377 | 1508 | window_set_background_color |
| 379 | 1516 | pbl_override_localtime |
| 380 | 1520 | animation_create |
| 384 | 1536 | animation_schedule |
| 388 | 1552 | animation_set_duration |
| 390 | 1560 | animation_set_implementation |
| 391 | 1564 | animation_unschedule |
| 462 | 1848 | text_layer_create |
| 463 | 1852 | text_layer_destroy |
| 465 | 1860 | text_layer_get_layer |
| 466 | 1864 | text_layer_get_text |
| 467 | 1868 | text_layer_set_background_color |
| 468 | 1872 | text_layer_set_font |
| 471 | 1884 | text_layer_set_text |
| 472 | 1888 | text_layer_set_text_alignment |
| 473 | 1892 | text_layer_set_text_color |
| 519 | 2076 | pbl_override_time |
| 622 | 2488 | layer_get_unobstructed_bounds_by_value |
| 624 | 2496 | app_unobstructed_area_service_subscribe |
| 625 | 2500 | app_unobstructed_area_service_unsubscribe |

This confirms that the real PBW directly exercises the time path described in
README.md:

    PBW time() trampoline
      -> g_pbl_system_tbl[519]
      -> pbl_override_time()
      -> DEFINE_SYSCALL(sys_get_time)
      -> svc #4
      -> __sys_get_time()
      -> rtc_get_time()

It also requires tick callbacks and app-supplied animation callbacks.
app_event_loop() obtains events through sys_get_pebble_event(), drops back to
unprivileged Thread mode, and then dispatches tick and animation handlers
directly on the app thread. S1 must therefore preserve the app identity, MPU
map, and unprivileged state across the entire event loop. The separate
mcu_call_unprivileged() re-entry path is needed only when code that is already
privileged must invoke an app callback; it is not implied by these direct
event-loop callbacks.

Most entries in this list are applib functions rather than DEFINE_SYSCALL
wrappers. They execute with the app's current privilege and directly access
the process-owned AppState and heap. A first integration therefore needs the
whole app RAM contract (loaded image, AppState, heap, stack, and guard), not
only an executable allocation for the 3024-byte PBW image.
