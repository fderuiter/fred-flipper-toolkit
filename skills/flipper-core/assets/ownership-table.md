# Ownership table

Fill for nontrivial resources (records, HAL handles, files, queues, mutexes, timers, threads, views/models, notification sequences, buffers retained by APIs, changed device state). Not required for a trivial pure function. Rules: [runtime-ui.md](../references/runtime-ui.md).

| Resource | Owner | Borrowers / producers | Acquire failure behavior | Callback context | Lifetime | Synchronization | Stop / join / drain | Release postcondition |
|---|---|---|---|---|---|---|---|---|
| example: RX stream buffer | app | serial IRQ (producer), worker (consumer) | allocation crashes on OOM; size bounded at compile time | IRQ → worker | app start → after RX stopped and worker joined | stream buffer, zero-timeout send in IRQ | rx_stop, set flag, wake, join | freed after no producer/consumer remains |
| | | | | | | | | |

Teardown order (write the actual dependency order, not reverse allocation by habit):

1.
2.

Partial-initialization paths covered:
