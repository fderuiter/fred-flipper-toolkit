# Corrected assumptions

Common claims about Flipper development that are wrong or incomplete at the baseline. Load when an assumption is challenged or a review finding depends on one.

| Assumption | Finding at official 1.5.1 / f7 | Claim | Consequence |
|---|---|---|---|
| Display is OLED | Monochrome 128×64 LCD | [C-HW-001] | Use LCD contrast/refresh constraints |
| 256 KB SRAM is the app heap | Physical SRAM is shared with firmware, radio stacks and loaded apps | [C-HW-001] | Measure heap; no fixed allowance |
| `const` data stays in flash | FAP sections are loaded into RAM | [C-FAP-001] | Budget code, constants and assets |
| `malloc` is forbidden; use `furi_alloc` | `malloc` is exported and backed by the firmware heap; `furi_alloc` is not in the API table | [C-MEM-001] [C-MEM-003] | Do not flag `malloc`; do not invent `furi_alloc` |
| Allocation failure returns NULL | Exhaustion and `malloc(0)` crash via `furi_check`; ISR allocation crashes | [C-MEM-002] | Bound sizes first; NULL checks don't make OOM recoverable |
| `printf` works / never works | Raw `printf` row is not exported; `__wrap_printf` is, and the build wraps calls | [C-PRINT-001] | Use `FURI_LOG_*`/`snprintf`; verify output route |
| Missing record close panics immediately | Holder count imbalance; destroy refused while held | [C-REC-001] | Balance borrows; report as leak, not crash |
| `RECORD_PASSTHROUGH` exists | Not established in inspected public surface | [C-REC-002] | Treat as invented until source proves it |
| All loops/blocking are wrong | App/worker loops may block with wakeup/cancel paths | [C-GUI-001] [C-CTX-001] | Judge by context, not by keyword |
| `furi_timer_stop` waits for the callback | Stop enqueues a command; free flushes the daemon | [C-TMR-001] [C-TMR-002] | Keep context alive until free returns |
| Every app needs ViewDispatcher + SceneManager | ViewPort is public and sufficient for simple UIs | [C-GUI-002] | Choose the smallest architecture |
| Render only on state change | Timed animation is legitimate when bounded and stopped when inactive | [C-GUI-001] | Review rate and stop path, not existence |
| Storage is asynchronous | Calls wait on the storage service and return counts | [C-STO-001] | Avoid in GUI/timer/IRQ; handle short I/O |
| rename/sync is atomic | Not established for power loss | [C-STO-002] | Temp file + recovery; no atomicity promise |
| All manifest fields mandatory | `appid` and `apptype` required generically; entry and catalog rules add more | [C-FAP-002] | Separate schema, runnable and catalog validity |
| `ufbt lint` is static analysis / `ufbt test` exists | Lint checks formatting; no test target | [C-BUILD-001] | Report lint as formatting only |
| FAP can be a BLE central | GAP peripheral; no exported client workflow | [C-BLE-001] [C-BLE-002] | Host or companion MCU |
| Flipper JS is Node/browser JS | mJS-derived subset with firmware modules | [C-JS-001] | Check module availability on the pinned fork |
| Emulator proves behavior | No comprehensive emulator; official unit tests run on-device | [C-EMU-001] [C-TEST-001] | Host tests + device tests, reported separately |
| A header in the firmware tree means FAP access | Needs export/library, implementation and build | [C-FAP-003] | Check `api_symbols.csv` and `fap_libs` |
| Fork APIs work on official firmware | Separate lineages with different exports | [C-FORK-001] | Tag examples by lineage; rebuild |
