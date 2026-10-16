# Device test plan

Hardware tests need explicit user authorization for each action class (deploy/launch, flashing, GPIO output, transmission, HID, tag write, appliance command). Writing this plan does not authorize running it.

| Item | Value |
|---|---|
| Device / hardware revision | |
| Firmware lineage / commit (from device info) | |
| App artifact SHA-256 | |
| Fixture, wiring, voltages (3.3 V unless verified) | |
| Authorized actions and scope | |
| Stop / safe-state procedure | |
| Data handling (no real secrets/private captures) | |

| Case | Steps | Expected | Observed | Repetitions | Result |
|---|---|---|---|---|---|
| Launch, navigate, short/long Back, exit | | | | | |
| Repeated open/work/exit; heap stable | | | | | |
| Settings: valid / old / corrupt / future / SD absent / interrupted save | | | | | |
| UART: busy handle, burst, errors, resync, cancel with active producer | | | | | |

Record actual observations only. A case not run stays `not-run`; compile or host results never fill this table.
