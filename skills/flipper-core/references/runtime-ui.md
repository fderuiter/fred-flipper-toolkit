# Runtime, ownership and UI

Load for native implementation or review of app lifecycle, callbacks, threads, timers, queues and GUI.

Extends the shared engineering rules (memory, context table, timer stop/free, queue policy, logging); it does not repeat them.

## Memory and primitives

- `malloc` is `pvPortMalloc`. Measure free heap and high-water marks where available; prefer fixed or reused buffers for sustained traffic; explicitly initialize state even though this allocator zeroes memory [C-MEM-001].
- `furi_message_queue_put/get` from IRQ require timeout 0 and a non-NULL message, otherwise `furi_check` fails [C-CTX-001].
- Furi timers run callbacks on the shared timer daemon. `furi_timer_alloc/start/stop/free` must not be called from IRQ [C-TMR-003].

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

## UI architecture

| Need | Choose |
|---|---|
| One custom screen, simple state | ViewPort + Canvas |
| Standard controls (menu, list, text input, dialogs) | View + modules |
| Several screens with routing | ViewDispatcher |
| Reusable screens with history/back stack | ViewDispatcher + SceneManager |

Module and dispatcher details that commonly break apps [C-GUI-003]:

- `VariableItemList` value counts and the current index are `uint8_t`: at most 255 choices per item. Map wide ranges to steps (for example 5–600 s in 5 s or preset steps) and convert index ↔ value explicitly.
- A View's `previous_callback` runs inside the dispatcher's Back-input handling. Return a view ID only; do storage or other slow work in the app thread after `view_dispatcher_run` returns, or from a custom event you send.
- Hand input to the app thread (message queue or custom event) instead of changing shared state inside the GUI input callback; lock models with `with_view_model` or a mutex. Draw only in the draw callback; request redraws (`view_port_update`, model commit) elsewhere [C-GUI-001].

Animation: start the refresh timer when the view becomes active, stop it when inactive or exiting, and avoid redraw storms from every incoming byte.

Input: define press/release/short/long/repeat handling, Back from nested views, cancel during long operations, and final exit. Show actionable busy/error/retry states. Notifications must not leave LED/vibration/speaker activity running.

Pinned official examples: [upstream-examples.md](upstream-examples.md). Recheck exports before reusing an example API in an external app.
