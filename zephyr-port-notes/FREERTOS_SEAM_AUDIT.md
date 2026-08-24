# FreeRTOS seam audit

This is the Z0.1 baseline inventory of direct FreeRTOS API use outside
`lib/os` and `third_party`. The scan covers C, C++, header, and assembly files
under the repository root; generated build directories and vendored Waf Python
sources are excluded. Comments are stripped before matching.

Counts below are API-shaped references (for example, `xQueueReceive(...)`).
Production counts include `src/`, `soc/`, `subsys/`, and public headers. Test
references are reported separately. The counts are the baseline before the
proof routing in this change; the proof removes eight semaphore references from
`src/fw/console/reliable_transport.c`.

## Summary and ranking

The mechanical score is from 0 to 4, where 4 means a direct, typed wrapper with
no scheduler or ISR policy required. Rank score is production references times
the mechanical score. A zero means the family needs design before bulk edits.

| Rank | Family | Production | Tests | Seam coverage before Z0.1 | Mechanical | Rank score |
|---:|---|---:|---:|---|---:|---:|
| 1 | Binary semaphores / mutexes | 109 | 5 | Partial: mutexes only | 4 | 436 |
| 2 | Queues | 51 | 27 | None | 2 | 102 |
| 3 | Delay / suspend / yield | 9 | 4 | Partial: tick conversion only | 4 | 36 |
| 4 | Task create / delete | 4 | 2 | None (`pebble_task_create` is firmware-local) | 1 | 4 |
| 5 | Critical sections | 59 | 0 | None | 0 | 0 |
| 6 | TCB / scheduler inspection | 52 | 4 | None | 0 | 0 |
| 7 | ISR variants | 15 | 0 | None | 0 | 0 |
| 8 | Event groups | 0 | 0 | None, currently unused | 0 | 0 |
| 9 | FreeRTOS software timers | 0 | 0 | None, currently unused | 0 | 0 |
| 10 | Task notifications | 0 | 0 | None, currently unused | 0 | 0 |

The semaphore score applies only to thread-context binary semaphores. Handles
that are also given from ISR context must stay on the FreeRTOS API until the ISR
wake/yield contract has been designed. Queue sets likewise make queues less
mechanical than their raw count suggests.

## Task create and delete

Production: **4**. Tests/stubs: **2**.

APIs: `vTaskDelete` (2), `xTaskCreateRestricted` (1), `xTaskCreate` (1).

Production files:

- `src/fw/kernel/pebble_tasks.c`
- `src/fw/process_management/process_manager.c`
- `src/fw/services/boot_splash/service.c`

Test files:

- `tests/stubs/stubs_task.h`

Coverage: **none in `pbl/os`**. `pebble_task_create()` centralizes most normal
firmware task creation, but its public parameters and handles are FreeRTOS
types. Task deletion has lifecycle, TLS, MPU, and task-registration policy, so
this should not be reduced to a thin backend wrapper yet.

## Queues

Production: **51**. Tests/fakes: **27**.

APIs: `xQueueReceive` (11), `xQueueCreate` (10),
`uxQueueMessagesWaiting` (7), `xQueueSendToBack` (7), `xQueueAddToSet` (5),
`xQueueSend` (4), `xQueueCreateSet` (2), `xQueueSelectFromSet` (2),
`xQueueReset` (2), `uxQueueSpacesAvailable` (1).

Production files:

- `src/fw/console/pulse2.c`
- `src/fw/kernel/events.c`
- `src/fw/process_management/app_manager.c`
- `src/fw/process_management/process_manager.c`
- `src/fw/process_management/worker_manager.c`
- `src/fw/services/accel_manager/service.c`
- `src/fw/services/event_service/service.c`
- `src/fw/services/hrm/hrm_manager.c`
- `src/fw/services/new_timer/new_timer.c`
- `src/fw/services/system_task/service.c`

Test files:

- `tests/fakes/fake_queue.c`
- `tests/fw/services/activity/test_activity.c`
- `tests/fw/services/test_hrm_manager.c`
- `tests/fw/test_app_manager.c`
- `tests/fw/test_process_manager.c`
- `tests/stubs/stubs_queue.h`

Coverage: **none**. Basic create/send/receive/count operations are mechanical,
but a useful seam needs item-size typing, reset/delete semantics, and timeout
policy. Queue sets in `events.c` and `system_task/service.c` do not have a 1:1
Zephyr equivalent. ISR sends are counted separately below.

## Binary semaphores and mutexes

Production baseline: **109**. Tests: **5**.

APIs: `xSemaphoreGive` (45), `xSemaphoreTake` (36),
`xSemaphoreCreateBinary` (18), `vSemaphoreDelete` (7), and legacy
`vSemaphoreCreateBinary` (3). There are no direct
`xSemaphoreCreateMutex`, recursive-mutex take, or recursive-mutex give calls;
firmware mutex users already use `pbl/os/mutex.h`.

Production files:

- `src/bluetooth-fw/nimble/gatt_client_discovery.c`
- `src/bluetooth-fw/nimble/init.c`
- `src/fw/applib/ui/app_window_stack.c`
- `src/fw/apps/prf/mfg_mic_asterix.c`
- `src/fw/comm/ble/gatt_client_subscriptions.c`
- `src/fw/console/pulse2.c`
- `src/fw/console/reliable_transport.c`
- `src/fw/drivers/display/sf32lb/display_jdi.c`
- `src/fw/drivers/display/sharp_ls013b7dh01/sharp_ls013b7dh01_nrf5.c`
- `src/fw/drivers/flash/flash_api.c`
- `src/fw/drivers/flash/flash_erase.c`
- `src/fw/drivers/i2c/common.c`
- `src/fw/drivers/i2c/nrf5.c`
- `src/fw/drivers/i2c/sf32lb.c`
- `src/fw/drivers/nrf5/qspi.c`
- `src/fw/kernel/task_timer.c`
- `src/fw/kernel/ui/modals/modal_manager.c`
- `src/fw/services/activity/activity.c`
- `src/fw/services/comm_session/default_kernel_sender.c`
- `src/fw/services/firmware_update/service.c`
- `src/fw/services/new_timer/new_timer.c`
- `src/fw/services/put_bytes/put_bytes.c`

Test files:

- `tests/fw/services/comm_session/test_session_send_buffer.c`
- `tests/fw/services/test_put_bytes.c`

Coverage before Z0.1: **partial**. `pbl/os/mutex.h` covers ordinary and recursive
mutexes, ownership assertions, and timed locking, but it lacked a recursive
destroy helper and an explicit forever-with-LR helper. It did not cover binary
semaphores. This change adds those thin mutex operations plus a thread-context
binary semaphore API. It routes all eight direct semaphore operations in
`reliable_transport.c`, leaving **101** production direct semaphore references.

The next routing batch converts two more complete, thread-context handles:

- `src/fw/services/firmware_update/service.c`: five references
- `src/fw/drivers/flash/flash_erase.c`: four references

Both initially-given semaphores are represented as an initially-empty
`semaphore_create()` followed by `semaphore_give()`. This preserves the legacy
constructor behavior in `firmware_update` and the explicit create/give sequence
in `flash_erase`. After both routing batches, **92** production direct semaphore
references remain; the **5** test references are unchanged. Handles that have
any ISR or critical-section use remain direct.

The three baseline `vSemaphoreCreateBinary` sites are not equivalent to
`xSemaphoreCreateBinary`: the legacy macro creates the semaphore in the given
state. One is routed above with an explicit initial give; the two remaining
sites must not be mechanically replaced with the new initially-empty semaphore
constructor.

## Event groups

Production: **0**. Tests: **0**.

No `xEventGroup*`, `vEventGroup*`, `EventGroupHandle_t`, or `EventBits_t` use
was found. Coverage: **none**, but no Z0 work is currently needed.

## Software timers and Pebble task timers

Direct FreeRTOS software-timer API references: **0** production and **0** test.
No `xTimer*`, `vTimer*`, `pvTimer*`, or `TimerHandle_t` use was found. One file,
`src/fw/services/data_logging/dls_endpoint.c`, includes `timers.h` without using
the API and can lose that include in a later include-cleanup pass.

Pebble's separate task-timer subsystem has 25 declarations, definitions, and
calls across:

- `src/fw/kernel/task_timer.c`
- `src/fw/kernel/task_timer.h`
- `src/fw/kernel/task_timer_manager.h`
- `src/fw/services/new_timer/new_timer.c`

Coverage: **none as a timer abstraction**. It is not a FreeRTOS software timer;
it is Pebble code coupled to `TickType_t`, a `SemaphoreHandle_t`, and the new
timer service queue. Port the semaphore and queue dependencies first, then keep
the existing task-timer policy intact above them.

## Critical sections

Production: **59**. Tests: **0**.

APIs/macros: `portEXIT_CRITICAL` (26), `portENTER_CRITICAL` (23),
`portIN_CRITICAL` (6), `taskENTER_CRITICAL` (2), `taskEXIT_CRITICAL` (2).

Production files:

- `soc/nrf/nrf52/sleep.c`
- `soc/sf32lb/sf32lb52x/sleep.c`
- `src/fw/applib/ui/animation.c`
- `src/fw/console/pulse2.c`
- `src/fw/console/serial_console.c`
- `src/fw/drivers/backlight/aw9364e.c`
- `src/fw/drivers/i2c/sf32lb.c`
- `src/fw/drivers/nrf5/hfxo.c`
- `src/fw/drivers/sf32lb52/qspi.c`
- `src/fw/drivers/task_watchdog.c`
- `src/fw/kernel/kernel_applib_state.c`
- `src/fw/kernel/reset.c`
- `src/fw/kernel/util/interval_timer.c`
- `src/fw/services/put_bytes/put_bytes.c`
- `subsys/logging/logging.c`
- `subsys/logging/pulse_logging.c`

Coverage: **none**. This family needs design before routing. The sites mix
interrupt masking, scheduler exclusion, early exits, nesting queries, and SoC
sleep sequencing. A single `critical_enter/exit` wrapper would hide materially
different semantics between FreeRTOS and Zephyr.

## Delay, suspend, resume, and yield

Production: **9**. Tests/stubs: **4**.

APIs: `vTaskSuspend` (5), `vTaskDelay` (2), `vTaskResume` (1), `taskYIELD` (1).

Production files:

- `src/fw/console/prompt_commands.c`
- `src/fw/kernel/fault_handling.c`
- `src/fw/kernel/pebble_tasks.c`
- `src/fw/process_management/process_manager.c`
- `src/fw/services/boot_splash/service.c`
- `src/fw/syscall/syscall.c`

Test files:

- `tests/fw/services/test_light.c`
- `tests/stubs/stubs_sleep.h`
- `tests/stubs/stubs_task.h`

Coverage: **partial only for duration conversion** through `pbl/os/tick.h`.
The two delay calls are good candidates for a millisecond sleep wrapper.
Suspend/resume and yield carry task-lifecycle and scheduling semantics and
should follow the task abstraction design.

## Direct TCB and scheduler operations

Production: **52**. Tests/stubs: **4**.

APIs: `xTaskGetSchedulerState` (10), `xTaskGetCurrentTaskHandle` (8),
`vTaskPrioritySet` (3), `uxTaskGetNumberOfTasks` (3), `uxTaskGetSystemState`
(3), `eTaskConfirmSleepModeStatus` (3), `ulTaskDebugGetStackedLR` (2),
`ulTaskDebugGetStackedPC` (2), `pvTaskGetThreadLocalStoragePointer` (2),
`vTaskSetThreadLocalStoragePointer` (2), `ulTaskGetStackStart` (2),
`xTaskGetIdleTaskHandle` (2), `vTaskStepTick` (2), `vTaskStartScheduler` (1),
`ulTaskDebugGetStackedControl` (1), `vTaskListWalk` (1), `pcTaskGetTaskName`
(1), `uxTaskGetStackHighWaterMark` (1), `vTaskAllocateMPURegions` (1),
`eTaskGetState` (1), and `xTaskGetTickCount` (1).

Production files:

- `soc/nrf/nrf52/freertos.c`
- `soc/qemu/freertos.c`
- `soc/sf32lb/sf32lb52x/freertos.c`
- `src/fw/console/pulse2.c`
- `src/fw/drivers/rtc/sf32lb.c`
- `src/fw/drivers/task_watchdog.c`
- `src/fw/kernel/core_dump.c`
- `src/fw/kernel/fault_handling.c`
- `src/fw/kernel/kernel_applib_state.c`
- `src/fw/kernel/pebble_tasks.c`
- `src/fw/kernel/reset.c`
- `src/fw/kernel/util/stack_info.c`
- `src/fw/kernel/util/task_telemetry.c`
- `src/fw/main.c`
- `src/fw/process_management/process_manager.c`
- `src/fw/services/system_task/service.c`
- `src/fw/syscall/syscall_internal.c`
- `src/fw/system/reboot_reason.c`
- `subsys/logging/logging.c`
- `subsys/logging/pulse_logging.c`

Test files:

- `tests/fw/test_data_logging.c`
- `tests/stubs/stubs_task.h`
- `tests/subsys/logging/test_pulse_logging.c`

Coverage: **none**. Current-task identity and a coarse scheduler-running query
could become small seams, but runtime statistics, stack-frame inspection, MPU
regions, TLS, tickless-idle stepping, and core-dump walks require separate
designs. They should not share a generic task wrapper merely because FreeRTOS
places them in `task.h`.

## ISR-only variants

Production: **15**. Tests: **0**.

APIs: `xSemaphoreGiveFromISR` (8), `xQueueSendToBackFromISR` (3),
`portYIELD_FROM_ISR` (3), `xQueueSendFromISR` (1).

Production files:

- `src/fw/apps/prf/mfg_mic_asterix.c`
- `src/fw/console/pulse2.c`
- `src/fw/drivers/display/sf32lb/display_jdi.c`
- `src/fw/drivers/display/sharp_ls013b7dh01/sharp_ls013b7dh01_nrf5.c`
- `src/fw/drivers/i2c/common.c`
- `src/fw/drivers/nrf5/qspi.c`
- `src/fw/kernel/events.c`
- `src/fw/services/new_timer/new_timer.c`
- `src/fw/services/system_task/service.c`

Coverage: **none**. These are intentionally untouched. FreeRTOS returns a
`higher_priority_task_woken` decision and requires an explicit yield; Zephyr's
ISR-safe semaphore and queue operations schedule differently. The seam must
define wakeup and post-ISR rescheduling semantics before any conversion.

## Task notifications

Production: **0**. Tests: **0**.

No `xTaskNotify*`, `ulTaskNotify*`, `vTaskNotify*`, or notification FromISR use
was found. Coverage: **none**, but no Z0 work is currently needed.

## FreeRTOS type coupling

API-call routing alone will not remove the header dependency. The current
production tree still contains these direct type references after both routing
batches:

| Type | References | Files |
|---|---:|---:|
| `TaskHandle_t` | 29 | 11 |
| `TaskParameters_t` | 11 | 10 |
| `TaskStatus_t` | 6 | 2 |
| `QueueHandle_t` | 33 | 14 |
| `QueueSetHandle_t` | 2 | 2 |
| `QueueSetMemberHandle_t` | 2 | 2 |
| `SemaphoreHandle_t` | 26 | 19 |
| `TickType_t` | 22 | 13 |

The most important leaks are `QueueHandle_t` and `SemaphoreHandle_t` in public
or cross-subsystem headers such as `include/pbl/drivers/i2c/definitions.h`,
`include/pbl/drivers/qspi_definitions.h`, `include/pbl/services/activity/activity_private.h`,
`include/pbl/services/hrm/hrm_manager_private.h`, `src/fw/kernel/events.h`, and
`src/fw/kernel/task_timer_manager.h`.

## FreeRTOS constant and result coupling

The call-family counts do not include result constants, timeout constants, or
configuration values. These current production references must be removed with
their owning family rather than replaced globally:

- `portMAX_DELAY`: 27 references in `src/bluetooth-fw/nimble/gatt_client_discovery.c`,
  `src/fw/applib/ui/app_window_stack.c`, `src/fw/apps/prf/mfg_mic_asterix.c`,
  `src/fw/console/pulse2.c`, both display drivers, `src/fw/drivers/flash/flash_api.c`,
  `src/fw/drivers/flash/flash_erase.c`, `src/fw/drivers/nrf5/qspi.c`,
  `src/fw/kernel/task_timer.c`, `src/fw/kernel/ui/modals/modal_manager.c`,
  `src/fw/process_management/process_manager.c`, `src/fw/services/alarms/alarm.c`,
  `src/fw/services/put_bytes/put_bytes.c`, and `src/fw/services/system_task/service.c`.
- `pdTRUE`: 18 references across 10 files; `pdFALSE`: 16 across 12 files;
  `pdPASS`: 2 in `src/fw/drivers/i2c/common.c`; `pdFAIL`: 2 in
  `src/fw/kernel/events.c` and `src/fw/process_management/process_manager.c`.
  These belong to queue, semaphore, task-create, and ISR return-value routing.
- `portBASE_TYPE`: 12 references in `include/pbl/drivers/i2c/definitions.h`,
  `src/fw/console/pulse2.c`, both display drivers, `src/fw/drivers/i2c/common.c`,
  `src/fw/drivers/i2c/sf32lb.c`, `src/fw/kernel/events.c`, and
  `src/fw/services/system_task/service.c`.
- `configTICK_RATE_HZ`: 6 references in `src/fw/drivers/rtc/nrf5.c`,
  `src/fw/kernel/task_timer.c`, `src/fw/services/activity/activity.c`,
  `src/fw/services/regular_timer/service.c`, and `src/fw/util/time/time.c`.
- `configMAX_PRIORITIES`: 5 references in `src/bluetooth-fw/nimble/init.c`,
  `src/fw/main.c`, `src/fw/services/boot_splash/service.c`, and
  `src/fw/services/new_timer/new_timer.c`.
- `taskSCHEDULER_RUNNING`: 8 references in `src/fw/console/pulse2.c`,
  `src/fw/kernel/fault_handling.c`, `src/fw/kernel/kernel_applib_state.c`,
  `src/fw/kernel/reset.c`, and `src/fw/system/reboot_reason.c`;
  `taskSCHEDULER_SUSPENDED`: 2 references in the two `subsys/logging` sources.

## Recommended routing order

1. Convert remaining thread-only binary semaphores, grouped by handle. Do not
   convert a handle if any producer uses `GiveFromISR`. Handle the legacy
   initially-given constructors explicitly.
2. Add and route a millisecond sleep operation for the two `vTaskDelay` sites.
3. Introduce basic typed queues for simple create/send/receive/count users.
   Keep queue sets and ISR queues out of the first pass.
4. Move `TaskHandle_t` and `TaskParameters_t` behind the existing
   `pebble_tasks` layer, then design delete/suspend/resume lifecycle semantics.
5. Port the custom task-timer subsystem once its semaphore and queue members no
   longer expose FreeRTOS types.
6. Design ISR wake/yield semantics, then convert ISR semaphores and queues.
7. Split scheduler/TCB work into current-task identity, scheduler state,
   telemetry, TLS/MPU, core dump, and tickless-idle projects.
8. Design critical-section APIs last and by intent (IRQ mask, scheduler lock,
   or spinlock), not as a textual replacement for `portENTER_CRITICAL`.

## Running direct-use tally

This tally is derived from the baseline counts above and includes the two
completed routing batches. Tests are listed separately and remain untouched.

| Family | Production remaining | Tests remaining |
|---|---:|---:|
| Binary semaphores / mutexes | 92 | 5 |
| Queues | 51 | 27 |
| Delay / suspend / yield | 9 | 4 |
| Task create / delete | 4 | 2 |
| Critical sections | 59 | 0 |
| TCB / scheduler inspection | 52 | 4 |
| ISR variants | 15 | 0 |
| Event groups | 0 | 0 |
| FreeRTOS software timers | 0 | 0 |
| Task notifications | 0 | 0 |
