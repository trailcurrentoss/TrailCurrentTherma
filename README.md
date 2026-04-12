# TrailCurrent Therma

Closed-loop thermostat for the [TrailCurrent](https://trailcurrent.com) open-source vehicle platform. ESP-IDF firmware. Three boards, three firmware artifacts, one purpose.

## Architecture

Therma is built from three ESP32-S3 boards. Only the first board is on the CAN bus; the other two are driven from the first over direct GPIO wires.

```
                       ┌────────────────────────────────────┐
                       │  TrailCurrent CAN bus  (500 kbps)  │
                       └──────────────┬─────────────────────┘
                                      │
                        ┌─────────────▼──────────────┐
                        │   Therma Controller        │
                        │   Waveshare ESP32-S3       │
                        │   -RS485-CAN               │
                        │                            │
                        │  • Owns desired temp       │
                        │  • Owns heat/cool mode     │
                        │  • Runs control loop       │
                        │  • 0x3F/0x40 broadcasts    │
                        │  • 0x41/0x42 requests in   │
                        │  • OTA + mDNS              │
                        └──┬────────────┬────────────┘
           HEAT_CMD_OUT   │            │   COOL_CMD_OUT
           HEAT_STATUS_IN │            │   COOL_STATUS_IN
                     ┌────▼───┐    ┌───▼────┐
                     │ Heater │    │ Cooler │
                     │ Relay  │    │ Relay  │
                     │ ES32-S3│    │ ES32-S3│
                     │ Relay- │    │ Relay- │
                     │  1CH   │    │  1CH   │
                     └────┬───┘    └────┬───┘
                          │             │
                       Heating        Cooling
                       device          device
```

- **Therma Controller** — Waveshare ESP32-S3-RS485-CAN (same module as [Bearing](https://github.com/trailcurrentoss/TrailCurrentBearing)). Only board on the CAN bus. Authoritative owner of:
  - Desired temperature (setpoint)
  - Hysteresis threshold (deadband)
  - Current heat/cool mode
  Persists setpoint and threshold to NVS so a power cycle preserves them. Consumes current ambient temperature from Borealis `EnvironmentSensorData` (`0x1F`).
- **Therma Heater Relay** — Waveshare [ESP32-S3-Relay-1CH](https://www.waveshare.com/wiki/ESP32-S3-Relay-1CH). No CAN, no WiFi. Reads one GPIO from the controller and drives the onboard relay on `GPIO47`. Mirrors the commanded state on a second GPIO back to the controller so a stuck driver is detectable from the bus.
- **Therma Cooler Relay** — Identical hardware and firmware to the heater relay, built with a different role flag for naming/versioning. Wired to the cooler instead of the heater.

### Design rules

- **Therma owns the desired temperature.** Modelled the same way as Torrent and Switchback: the device that holds the value is the authoritative broadcaster. Any other device (PWA, wall panels, Farwatch cloud, Headwaters, …) that wants to change it sends a `ThermaSetDesiredRequest` (`0x41`). Therma validates, clamps, persists to NVS, and then the next broadcast on `ThermaDesiredTemperature` (`0x3F`) carries the new authoritative value. **Displays must always render the value from Therma's broadcast — never a locally-held copy.**
- **Therma owns the heat/cool mode.** It is the single source of truth for whether heating or cooling is currently on. No other device infers this.
- **Mutually exclusive outputs.** Heating and cooling can never be on simultaneously. Both off (idle) is allowed. Enforced in `relay_io_set_mode()`: both outputs drop, a ≥50 ms dwell, then the new one is raised.
- **Hysteresis prevents chatter.** Default deadband is 0.5 °C, overridable via NVS and `ThermaSetThresholdRequest` (`0x42`). On top of hysteresis there is also a 5 s minimum dwell in any non-idle state.
- **Fail-safe off** whenever: CAN bus-off, no Borealis temperature for >10 s, or a relay feedback pin disagrees with the commanded mode.

## CAN Bus Protocol

Four new message IDs are added to [TrailCurrent.dbc](https://github.com/trailcurrentoss/TrailCurrentDocumentation/blob/main/TrailCurrent.dbc) by this project. Detailed byte layouts are in [CAN_BUS_REFERENCE.md](https://github.com/trailcurrentoss/TrailCurrentDocumentation/blob/main/10_Reference/CAN_BUS_REFERENCE.md#therma---closed-loop-thermostat-0x3f-0x42).

| ID | Name | Direction | Rate | Purpose |
|---|---|---|---|---|
| `0x3F` | `ThermaDesiredTemperature` | Therma → bus | 1 Hz | Authoritative setpoint + threshold. Displays use this. |
| `0x40` | `ThermaStatus` | Therma → bus | 1 Hz | Authoritative mode, current temp, relay feedback, faults. |
| `0x41` | `ThermaSetDesiredRequest` | bus → Therma | event | Request a new setpoint. Any device can send. |
| `0x42` | `ThermaSetThresholdRequest` | bus → Therma | event | Request a new deadband. Any device can send. |

Therma also uses the existing common IDs:

| ID | Name | Direction | Purpose |
|---|---|---|---|
| `0x00` | OTA trigger | bus → Therma | Start OTA if this MAC matches (controller only) |
| `0x01` | WiFi config | bus → Therma | Provision WiFi credentials over CAN |
| `0x02` | Discovery trigger | bus → Therma | mDNS advertise for Headwaters discovery |
| `0x04` | Firmware version report | Therma → bus | Broadcast at boot |
| `0x1F` | Borealis `EnvironmentSensorData` | bus → Therma | Current ambient temperature (control input) |

## Hardware

### Controller board

**Waveshare ESP32-S3-RS485-CAN**. Same board Bearing uses. Reference: [docs](https://www.waveshare.com/esp32-s3-rs485-can.htm).

| Function | GPIO | Notes |
|---|---|---|
| CAN TX | 15 | Onboard SN65HVD230 |
| CAN RX | 16 | Onboard SN65HVD230 |
| `HEAT_CMD_OUT` | 4 | Drives heater relay's `CMD_IN` |
| `HEAT_STATUS_IN` | 5 | Reads heater relay's `STATUS_OUT`, pulldown |
| `COOL_CMD_OUT` | 6 | Drives cooler relay's `CMD_IN` |
| `COOL_STATUS_IN` | 7 | Reads cooler relay's `STATUS_OUT`, pulldown |

### Relay boards

**Waveshare [ESP32-S3-Relay-1CH](https://www.waveshare.com/wiki/ESP32-S3-Relay-1CH)**. Two of them, one per role.

| Function | GPIO | Notes |
|---|---|---|
| `RELAY_DRIVE` | 47 | Board-fixed, drives the coil |
| `CMD_IN` | 3 | Input from controller's `HEAT_CMD_OUT` / `COOL_CMD_OUT`, pulldown |
| `STATUS_OUT` | 4 | Output back to controller, mirrors commanded relay state |

All three boards share a **common ground**. 3.3 V logic on both ends — no level shifting needed.

## Repository layout

```
TrailCurrentTherma/
├── build-all.sh               # Build all 3 firmwares + merged binaries
├── scripts/
│   ├── flash_controller.sh    # USB-flash helpers (PORT=... to override)
│   ├── flash_heater_relay.sh
│   └── flash_cooler_relay.sh
├── controller/                # ESP-IDF project #1 (Waveshare ESP32-S3-RS485-CAN)
│   ├── CMakeLists.txt
│   ├── sdkconfig.defaults
│   ├── partitions.csv         # dual OTA slots
│   └── main/
│       ├── main.c             # app_main
│       ├── can_handler.c      # TWAI init + RX dispatch + 1 Hz periodic TX
│       ├── thermostat.c       # state machine: idle/heating/cooling + hysteresis
│       ├── relay_io.c         # GPIO cmd out + feedback in, mutex enforced
│       ├── state_store.c      # NVS setpoint + threshold
│       ├── wifi_config.c      # NVS creds + CAN provisioning (copied from Bearing)
│       ├── ota.c              # HTTP POST /ota (copied from Bearing)
│       ├── discovery.c        # mDNS + version TXT (copied from Bearing)
│       └── board.h            # pins + CAN IDs + tuning constants
├── relay/                     # ESP-IDF project #2 (Waveshare ESP32-S3-Relay-1CH)
│   ├── CMakeLists.txt
│   ├── sdkconfig.defaults
│   ├── partitions.csv         # minimal single-app, no OTA
│   └── main/
│       ├── CMakeLists.txt     # reads THERMA_RELAY_ROLE cache var
│       └── main.c             # GPIO loop: read CMD_IN, drive RELAY_DRIVE, echo STATUS_OUT
└── EDA/                       # KiCAD hardware design (controller carrier board)
```

## Building

Requires [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/index.html) 5.1 or newer.

```bash
# Build all three firmwares in one go
./build-all.sh
```

Or individually:

```bash
# Controller
idf.py -C controller -B controller/build build

# Heater relay (one build of the relay/ sources, role=HEATER)
idf.py -C relay -B relay/build_heater -DTHERMA_RELAY_ROLE=HEATER build

# Cooler relay (second build of the same sources, role=COOLER)
idf.py -C relay -B relay/build_cooler -DTHERMA_RELAY_ROLE=COOLER build
```

Separate `-B` directories are mandatory so the two relay builds don't overwrite each other. The relay role flag only affects log tag and version banner — core logic is identical in both artifacts.

Each build produces two binaries:

- **`*.bin`** — app-only, consumed by the OTA HTTP endpoint (controller only).
- **`*_merged.bin`** — full flash image (bootloader + partition table + ota_data + app), flashable from `0x0` via the web flasher or `esptool.py`.

`build-all.sh` produces all six artifacts:

```
controller/build/therma_controller.bin              # app-only (for OTA)
controller/build/therma_controller_merged.bin       # merged  (for web flasher)
relay/build_heater/therma_heater_relay.bin          # app-only (unused; no OTA)
relay/build_heater/therma_heater_relay_merged.bin   # merged  (for web flasher)
relay/build_cooler/therma_cooler_relay.bin          # app-only (unused; no OTA)
relay/build_cooler/therma_cooler_relay_merged.bin   # merged  (for web flasher)
```

## Flashing

### Web flasher (recommended for first flash of any board)

The TrailCurrent web flasher lists all three Therma firmwares as separate entries in the module dropdown:

- **Therma Controller — Thermostat**
- **Therma Heater Relay**
- **Therma Cooler Relay**

Plug the board in via USB-C, pick the right entry, pick the release, click Flash. No local tooling required.

### Local USB flash (during development)

```bash
PORT=/dev/ttyACM0 ./scripts/flash_controller.sh
PORT=/dev/ttyACM1 ./scripts/flash_heater_relay.sh
PORT=/dev/ttyACM2 ./scripts/flash_cooler_relay.sh
```

Each script wraps `idf.py -C <dir> -B <builddir> -p $PORT flash monitor`.

### OTA (controller only)

The controller participates in the standard TrailCurrent OTA flow via CAN `0x00`. The relay boards have no WiFi/HTTP stack and are flashed via USB only.

## Releasing

Following the Switchback / Torrent convention, attach **all six** binaries from a `build-all.sh` run to a single GitHub release:

```bash
gh release create v0.1.0 \
    controller/build/therma_controller.bin \
    controller/build/therma_controller_merged.bin \
    relay/build_heater/therma_heater_relay.bin \
    relay/build_heater/therma_heater_relay_merged.bin \
    relay/build_cooler/therma_cooler_relay.bin \
    relay/build_cooler/therma_cooler_relay_merged.bin
```

The web flasher at `flash.html` uses the `therma_<role>_merged.bin` filename prefix to decide which of the three merged binaries to push for each module-dropdown selection.

## License

MIT — see [LICENSE](LICENSE).
