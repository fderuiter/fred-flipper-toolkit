# Annotated upstream examples (pinned, linked not copied)

All links are pinned to official firmware 1.5.1 `9e810dc399e027e48ce07751df72168abbb19065` [C-BASE-001]. Existence of each `application.fam` was rechecked 2026-10-16; the examples were **not compiled or run by FRED**. Firmware sources are GPL-3.0: link or study them; do not paste them into MIT-licensed material. Before reusing an API from an example in an external app, confirm it is exported in `api_symbols.csv` [C-FAP-003].

Base: `https://github.com/flipperdevices/flipperzero-firmware/tree/9e810dc399e027e48ce07751df72168abbb19065/applications/examples/`

| Example | Use it to study | Watch for |
|---|---|---|
| [example_view_dispatcher](https://github.com/flipperdevices/flipperzero-firmware/tree/9e810dc399e027e48ce07751df72168abbb19065/applications/examples/example_view_dispatcher) | Two views (Widget, Submenu) switched through ViewDispatcher without SceneManager | Shows that scenes are optional [C-GUI-002] |
| [example_view_holder](https://github.com/flipperdevices/flipperzero-firmware/tree/9e810dc399e027e48ce07751df72168abbb19065/applications/examples/example_view_holder) | Single view with ViewHolder and an API lock for Back/exit | Exit synchronization pattern |
| [example_thermo](https://github.com/flipperdevices/flipperzero-firmware/tree/9e810dc399e027e48ce07751df72168abbb19065/applications/examples/example_thermo) | GUI, input, worker thread, 1-Wire sensor on GPIO | Electrical setup is the user's responsibility; verify pin/voltage [C-GPIO-001] |
| [example_event_loop](https://github.com/flipperdevices/flipperzero-firmware/tree/9e810dc399e027e48ce07751df72168abbb19065/applications/examples/example_event_loop) | FuriEventLoop with timers, mutex, event flags, stream buffer, multiple sources | Producer thread + stream buffer handoff; compare with IRQ rules [C-CTX-001] |
| [example_adc](https://github.com/flipperdevices/flipperzero-firmware/tree/9e810dc399e027e48ce07751df72168abbb19065/applications/examples/example_adc) | ADC acquire/read/release with on-screen output | Input range/reference; release on exit [C-BUS-001] |
| [example_apps_data](https://github.com/flipperdevices/flipperzero-firmware/tree/9e810dc399e027e48ce07751df72168abbb19065/applications/examples/example_apps_data) / [example_apps_assets](https://github.com/flipperdevices/flipperzero-firmware/tree/9e810dc399e027e48ce07751df72168abbb19065/applications/examples/example_apps_assets) | App data/asset folders on SD | Check every storage return value [C-STO-001] |
| [example_ble_beacon](https://github.com/flipperdevices/flipperzero-firmware/tree/9e810dc399e027e48ce07751df72168abbb19065/applications/examples/example_ble_beacon) | Extra BLE beacon advertising with SceneManager | Advertising is not central/GATT-client capability [C-BLE-002] |
| [example_plugins](https://github.com/flipperdevices/flipperzero-firmware/tree/9e810dc399e027e48ce07751df72168abbb19065/applications/examples/example_plugins) / [example_plugins_advanced](https://github.com/flipperdevices/flipperzero-firmware/tree/9e810dc399e027e48ce07751df72168abbb19065/applications/examples/example_plugins_advanced) | FAP plugins and API resolution | Loaded code costs RAM [C-FAP-001] |
| [example_images](https://github.com/flipperdevices/flipperzero-firmware/tree/9e810dc399e027e48ce07751df72168abbb19065/applications/examples/example_images), [example_custom_font](https://github.com/flipperdevices/flipperzero-firmware/tree/9e810dc399e027e48ce07751df72168abbb19065/applications/examples/example_custom_font), [example_number_input](https://github.com/flipperdevices/flipperzero-firmware/tree/9e810dc399e027e48ce07751df72168abbb19065/applications/examples/example_number_input) | Assets, fonts, standard input module | Asset size counts toward loaded RAM |

Other references:

- Native file formats, including Infrared `.ir`: [documentation/file_formats](https://github.com/flipperdevices/flipperzero-firmware/tree/9e810dc399e027e48ce07751df72168abbb19065/documentation/file_formats) [C-FMT-001].
- uFBT usage at 0.2.6: [README](https://github.com/flipperdevices/flipperzero-ufbt/blob/093ab78058f19b11eab82020989aa4e123521490/README.md) [C-BASE-004]. `ufbt create APPID=<id>` generates the official template; FRED does not ship its own.
- Community tutorials such as [jamisonderek/flipper-zero-tutorials](https://github.com/jamisonderek/flipper-zero-tutorials/tree/2aeac9189f6e6ad7d9c2d857c40cd1afb43882f9) (MIT) explain many patterns but target varying firmware; recompile against the baseline before trusting an API [C-FORK-001].
