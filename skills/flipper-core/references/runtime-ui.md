# Runtime, ownership and UI

Load for native implementation or review of app lifecycle, callbacks, threads, timers, queues and GUI.

## Memory

- 256 KB physical SRAM is shared; FAP sections are loaded into RAM, so code size and constants cost heap [C-HW-001] [C-FAP-001]. Measure free heap and high-water marks where available; there is no universal app allowance.
- `malloc` → `pvPortMalloc`. At 9e810dc it crashes with "out of memory" or "malloc(0)" instead of returning NULL, and crashes in ISR mode [C-MEM-001] [C-MEM-002]. Consequences: validate sizes and `count × size` overflow before allocation; prefer fixed or reused buffers for sustained traffic; never allocate in IRQ callbacks; do not write "if NULL, recover" as an OOM strategy.
- Explicitly initialize state even though this allocator zeroes memory.

## Contexts and primitives

- ViewPort draw/input callbacks are documented as running on the GUI thread [C-GUI-001]. Keep them short; read a snapshot of the model; post input to the app queue.
- ViewDispatcher custom/tick/navigation callbacks run in the thread that runs the dispatcher [C-GUI-001].
- `furi_message_queue_put/get` from IRQ require timeout 0 and a non-NULL message, otherwise `furi_check` fails [C-CTX-001]. Check the returned status; decide a full-queue policy.
- Furi timers run callbacks on the shared timer daemon. `furi_timer_alloc/start/stop/free` must not be called from IRQ [C-TMR-003].
- `furi_timer_stop` issues `xTimerStop` and returns; it does not wait for an in-flight callback, although `timer.h` describes it as synchronous [C-TMR-001]. `furi_timer_free` deletes the timer then calls `furi_timer_flush`, which pends a function on the daemon and waits for it [C-TMR-002]. Therefore keep everything the callback touches alive until `furi_timer_free` returns, and never free or flush a timer from a timer callback.

## Ownership table and teardown

Fill [ownership-table.md](../assets/ownership-table.md) for: records, HAL handles, files, queues, mutexes, timers, threads, views/models, notification sequences, buffers retained by APIs, and changed device state.

Records: `furi_record_open` increments a holder count and waits until the record exists; `furi_record_close` decrements; destroy is refused while holders remain [C-REC-001]. Borrowed services are never freed by the app. A typo in a record name blocks forever.

Teardown checklist (order follows dependencies):

1. Set a stopping flag/state visible to producers.
2. Stop or unregister producers: timers (keep context alive), IRQ callbacks (`furi_hal_serial_async_rx_stop`-style APIs for the handle in use), input callbacks.
3. Establish quiescence: free/flush timers, release HAL handles only after their callbacks are stopped.
4. Wake workers (flag + event/queue message), join them without holding a mutex they need.
5. Drain queues under an explicit ownership policy for queued pointers.
6. Detach views/ViewPort from GUI before freeing them; remove views from the dispatcher before freeing the dispatcher.
7. Restore only hardware/configuration this app changed.
8. Free memory; close records last.

Partial initialization: track each acquired resource so every failure path releases exactly that set.

## Queues and cancellation

Policy per queue: replaceable ticks/status → coalesce; telemetry → count drops; commands → reject or flow-control; cancellation/Back/exit → separate flag or reserved path that cannot be dropped. Size from worst-case arrival × longest service gap.

## UI architecture

| Need | Choose |
|---|---|
| One custom screen, simple state | ViewPort + Canvas |
| Standard controls (menu, list, text input, dialogs) | View + modules |
| Several screens with routing | ViewDispatcher |
| Reusable screens with history/back stack | ViewDispatcher + SceneManager |

ViewPort, ViewDispatcher and SceneManager are all exported [C-GUI-002]; SceneManager is optional, not mandatory.

Module and dispatcher details that commonly break apps [C-GUI-003]:

- `VariableItemList` value counts and the current index are `uint8_t`: at most 255 choices per item. Map wide ranges to steps (for example 5–600 s in 5 s or preset steps) and convert index ↔ value explicitly.
- A View's `previous_callback` runs inside the dispatcher's Back-input handling. Return a view ID only; do storage or other slow work in the app thread after `view_dispatcher_run` returns, or from a custom event you send.
- Hand input to the app thread (message queue or custom event) instead of changing shared state inside the GUI input callback. View models need a locking or snapshot strategy (`with_view_model` or a mutex). Draw only in the draw callback; request redraws (`view_port_update`, model commit) rather than drawing elsewhere.

Animation: timed refresh is legitimate. Choose a modest, justified rate; start the timer when the view becomes active and stop it when inactive or exiting; avoid redraw storms from every incoming byte.

Input: define press/release/short/long/repeat handling, Back from nested views, cancel during long operations, and final exit. Show actionable busy/error/retry states. Notifications must not leave LED/vibration/speaker activity running.

## Logging

Use `FURI_LOG_E/W/I/D` with a tag and bounded `snprintf`. Raw `printf` is not exported; `__wrap_printf` is [C-PRINT-001]. Do not log from IRQ or tight timing paths; verify where output goes before relying on it.

## Upstream examples (pinned, linked not copied)

See the repository's `examples/upstream-index.md` for annotated, commit-pinned examples. Recheck exports before reusing any example API in an external app.
