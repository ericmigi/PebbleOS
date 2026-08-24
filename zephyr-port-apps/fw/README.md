# PebbleOS core firmware bring-up

This application is the P2 unified-firmware bring-up for `pt2`. It compiles the
real firmware entry, Pebble task registry, launcher event loop, kernel event queues, event
service/client dispatch, system background task, NewTimer service, regular
timer, and tick timer service into one Zephyr image.

The `CONFIG_PEBBLE_ZEPHYR_CORE_BOOT` paths retain the production ownership and
queue priorities while limiting the event ABI to tick, callback, and
subscription events. Zephyr supplies the threads, queues, queue sets, mutexes,
semaphores, ticks, and RTC beneath those sources.

Expected UART milestones are:

```text
FW_BOOT
FW_TASK NewTimers up
FW_TASK KernelBackground up
FW_TASK KernelMain up
FW_SERVICES_OK
FW_EVENT_LOOP_UP
FW_TICK HH:MM:SS
FW_TIMER dispatched
```

The boot also prints one `FW_STUB` line for each intentionally deferred service
family: board drivers, display/compositor, PFS/resources, BLE/communications,
app/worker process launch, and watchdog/analytics.
