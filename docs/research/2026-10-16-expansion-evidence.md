# Expansion evidence and decisions (issue #11)

Checked 2026-10-16. Read-only source and documentation research. The JavaScript host tests compiled and ran code from the pinned official firmware only. No third-party code was executed, installed or copied. Claim IDs refer to `skills/flipper-core/references/sources.json`.

## Decisions

| Candidate expansion | Decision | Reason |
|---|---|---|
| `flipper-js` implementation skill | **Deferred** | The runtime differs sharply from generic JavaScript (below), and on-device module behavior is untested. The demand threshold in [maintenance.md](../maintenance.md#expansion-threshold) is not met. JS stays feasibility-only, now with concrete limits. |
| Momentum / Unleashed support | **Deferred; no support claimed** | Measured API differences (C-FORK-001), and the loader's major-only check (C-FAP-004) decides what loads. Qualification would need fork SDK builds and device tests per lineage. |
| Xtreme | **Historical only** | Archived. API 58.1 with 336 removed exports and toolchain 33. |
| Community emulator as oracle | **Rejected** | Unimplemented imports silently return 0; there is no scheduler, peripherals or API check, and no license (C-EMU-001). |
| Adapting an MCP / device-control project | **Rejected for v0.1** | None of the three audited projects gates physical actions in code (below). A future adapter must separate build, deploy and physical tools and enforce scope outside the model. |
| Copying third-party skill or boilerplate code | **None selected** | malskill, the boilerplate and the emulator have no license (or an empty one); stm32-embedded-skill has only an informal README statement. |
| Appliance / wiring guidance | **Narrowed** | GPIO limits resolved as far as sources allow (C-GPIO-001). Appliance protocol roles still need per-appliance inspection; the BLE stress test stands. |

No expansion was promoted to an implementation issue: no candidate meets the threshold of three distinct real tasks plus a measured benchmark gap.

## JavaScript (mJS fork)

Source: `lib/mjs` at `9e810dc` is a vendored cesanta/mjs carrying the GPL-2.0/commercial headers. Flipper changes include a Furi platform layer, `let`/`const`, typed arrays, `fn.apply`, `Number.toString(base)` and a stop flag; JSON setup is commented out (`mjs_builtin.c`).

Host tests of the core interpreter ([evidence](evidence/2026-10-16-mjs/README.md); not device behavior):

| Works | Fails |
|---|---|
| `let`/`const` with block scope (but `const` reassignment is silently allowed); globals read and written from functions; named function expressions; `this` in methods; `Array.push`/`splice`/`length`; string `indexOf`/`slice`/`toUpperCase`/`[i]`; `for-in` (reverse order); `ArrayBuffer`/`DataView` | Closures over outer locals (`[n] is not defined`); `try`/`catch`/`throw` (not implemented; `die()` aborts); arrow functions; template literals; `class`, `new`, `switch`, `do`, `delete`, regex, `for-of`, destructuring, spread, async/Promise; `JSON`; `Math` as a global; `Array.map`/`forEach`/`join`; string `split`/`substring`/`trim`; `==`/`!=` ("Use ==="); implicit number/string conversion; missing semicolons; printing objects |

Quirks seen on the host: `String.at()` returns a char code, `1/0` gives NaN, floats print with `%f`, and recursion and array growth are super-linear.

Modules at the pin (`applications/system/js_app/modules`): badusb, event_loop, flipper, gpio, gui (16 view plugins), math, notification, serial, storage, plus `tests` in unit-test builds. JS SDK `@flipperdevices/fz-sdk` 1.0.0 (JS_SDK 1.0). The JS thread stack is 8 KiB; parser nesting stops at 512 levels; FFI takes at most 6 arguments. Momentum and Unleashed add blebeacon, i2c, spi, infrared, subghz, usbdisk and vgm, so fork scripts using these will not run on official firmware.

Consequence: guidance must not assume closures, exceptions, JSON or array higher-order functions. TypeScript output must avoid them; the SDK's esbuild configuration marks arrow, class, async/await, destructuring and for-of as unsupported (C-JS-001).

## Firmware forks

See C-FORK-001 and the [compatibility reference](../../skills/flipper-core/references/compatibility.md#other-lineages). Toolchain pins: official, Momentum and Unleashed use toolchain 39; Xtreme uses 33. Comparator exits: Momentum 1 (removed, changed, major), Unleashed 0, Xtreme 1.

## Simulator

ZDStudios `fapemu` at `23edb1b` runs FAP Thumb code on Unicorn with about 152 Python handlers for API 87.1. 19 of these are no-ops, and any other import returns 0 silently. It has no scheduler, peripherals, radios, ViewDispatcher or SceneManager, and no API-version check. Its storage maps to a host folder without `..` containment (inferred from code). The top-level README contradicts itself, and LICENSE was emptied in commit `095f703`. Use it at most for relocation or import-list smoke checks, and only with `--trace` showing no unknown calls.

## MCP / AI-control projects (read only, not executed)

| Project @ pin | License | Physical / radio tools | Gating in code | Other risks | Verdict |
|---|---|---|---|---|---|
| yairhdz24/flipper-mcp @ `9dcb774` | MIT | GPIO, LED, vibro, IR send, Sub-GHz TX/RX, raw `flipper_cli` | None (instruction text only) | `appid` is validated only when scaffolding, so `fap_build`/`fap_deploy` path joins allow traversal into an arbitrary directory whose `application.fam` is then evaluated; host file upload is unrestricted; the first matching USB device is used; dependencies are unpinned | Reference only |
| pogorelov-labs/flipper-ble-mcp @ `da83716` | MIT (+BSD-3 protobuf) | Sub-GHz/IR transmit, GPIO write, BadUSB keystrokes | Allowlist only in a healthwatch `assert` (disabled by `-O`) | Any BLE name containing "flip" matches; argument splitting into daemon argv; token compare not constant-time; macOS only | Reference only (socket + token pattern noted) |
| busse/flipperzero-mcp @ `36948b7` | MIT | BadUSB execute, music | A model-supplied `confirm` flag | Plaintext unauthenticated Wi-Fi transport; first matching device | Reference only |

## GPIO electrical limits

See C-GPIO-001. Header pins are 3.3 V logic behind 51 Ω series resistors, with 20 mA absolute maximum per pin and 100 mA total. 5 V tolerance is conditional, and firmware 1.5.1 puts header pins in analog mode at early init, which removes it. The official pages conflict. Still unresolved: the primary AN4899 text, the schematic PDFs (official links expired) and the ESD part ratings.
