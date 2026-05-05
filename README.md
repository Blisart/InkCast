# Inkcast

**English** | [Русский](README.ru.md)

Battery-powered e-paper weather station for the wall, built with an ESP32-S3, a 3.97" display, and a 3D-printed enclosure.

Typical power consumption is about `5-6 mAh/day` at the default 2-hour refresh interval, which translates to roughly `~1 year on a 2000 mAh cell` or `~1.5 years on a 3000 mAh cell`.

## Key Features

- Long battery life thanks to deep sleep between refresh cycles.
- Large 800x480 e-paper dashboard with current weather, a 48-hour chart, and a 6-day forecast.
- Fast screen refresh tuned for practical day-to-day use on a large 3.97" panel.
- Built-in web admin panel for Wi-Fi, weather provider, location, and refresh interval changes without reflashing.
- Two weather backends: free keyless Open-Meteo and optional OpenWeatherMap One Call 3.0.
- Desktop simulator in [`sim/`](sim/) for iterating on the UI without touching hardware.
- Published 3D-printable enclosure files for a complete wall-mounted device.

![Inkcast device](assets/inkcast.png)

Inkcast shows current conditions, a 48-hour temperature and precipitation chart, a 6-day forecast, battery level, Wi-Fi status, sunrise/sunset times, and a sunny-day streak on a display that stays readable without backlight.

## Displayed Data

- Current weather block with icon, feels-like temperature, min/max values, precipitation probability, sunrise, sunset, and sunny-day counter.
- 48-hour chart with temperature curve and precipitation bars.
- 6-day forecast cards with daily icons and min/max temperatures.
- Automatic timezone detection from weather API coordinates.
- Configurable refresh interval from 1 to 1440 minutes.
- Runtime settings storage in NVS, so values survive sleep cycles and reboots.
- Debug mode that exposes logs and the admin UI without changing firmware.
- Access point fallback for first-time Wi-Fi provisioning or broken saved credentials.

## Weather Sources

- **Open-Meteo**: free, no API key required, multiple forecast models are available.
- **OpenWeatherMap One Call 3.0**: requires an API key, useful if you prefer OWM as the backend.

By default the device refreshes once every 2 hours, stores runtime settings in NVS, and spends the rest of the time in deep sleep.

## Hardware

| Part | Notes |
|---|---|
| LILYGO T-Energy-S3 | ESP32-S3 board with 18650 battery support and power management |
| GDEM0397T81P | 3.97" black-and-white e-paper display, 800x480 |
| DESPI-C02 | Good Display SPI adapter board for the panel |

## Enclosure

The enclosure is designed for day-to-day use, with external access to USB-C for charging/flashing and the power switch on the back side.

STL files:
- [Printables](https://www.printables.com/model/1709801-inkcast)
- [Thingiverse](https://www.thingiverse.com/thing:7346784)

| USB-C opening | Power switch opening |
|---|---|
| ![Enclosure USB-C cutout](assets/enclosure-usb.png) | ![Enclosure switch cutout](assets/enclosure-switch.png) |

## Fasteners

| Item | Qty | Purpose |
|---|---|---|
| M3 heat-set insert | 4 | Threaded inserts for the printed case standoffs |
| M3x8 DIN 912 screw | 8 total | Case assembly |
| M2x6 screw | 4 | Mounting the T-Energy-S3 board |

## Wiring

### E-paper -> DESPI-C02

Insert the display FPC cable into the DESPI-C02 connector:
1. Lift the FPC latch.
2. Insert the cable with contacts facing down.
3. Close the latch.

### DESPI-C02 -> T-Energy-S3

| DESPI-C02 | T-Energy-S3 | GPIO | Function |
|---|---|---|---|
| VCC | 3.3V | - | Display power |
| GND | GND | - | Ground |
| DIN | MOSI | 11 | SPI data |
| CLK | SCK | 12 | SPI clock |
| CS | - | 10 | Chip select |
| DC | - | 9 | Data/command |
| RST | - | 8 | Reset |
| BUSY | - | 7 | Busy signal |

Check the silkscreen on your T-Energy-S3 revision before soldering or wiring. GPIO numbering can differ across board revisions.

### Diagram

```text
  T-Energy-S3                DESPI-C02              GDEM0397T81P
 +------------+           +-----------+           +-------------+
 |        3.3V|-----------|VCC        |           |             |
 |         GND|-----------|GND   [FPC connector]--|  FPC cable  |
 |      GPIO11|-----------|DIN        |           |             |
 |      GPIO12|-----------|CLK        |           +-------------+
 |      GPIO10|-----------|CS         |
 |       GPIO9|-----------|DC         |
 |       GPIO8|-----------|RST        |
 |       GPIO7|-----------|BUSY       |
 |            |           +-----------+
 |       GPIO3| <- battery ADC via onboard divider
 |       GPIO6| <- debug jumper (to GND = debug/admin mode)
 |            |
 |    [18650] |
 +------------+
```

## Debug Mode

The firmware automatically enters debug mode when at least one of these conditions is true:
- USB is connected to a computer.
- GPIO 6 is shorted to GND with the debug jumper.

In debug mode:
- Deep sleep is disabled.
- Serial logging stays available.
- The web admin panel becomes available.
- If saved Wi-Fi credentials fail, the device starts the fallback access point `ESP-Weather-Setup`.

Important: while the USB cable is connected to a computer, the device will not return to deep sleep.

## Firmware Setup

All settings can be provided in two ways:
- Runtime settings through the web admin panel, stored in NVS.
- Defaults in `config.h`, used on first boot or after resetting NVS.

Runtime values take priority over compile-time defaults.

### 1. Create `secrets.h`

```bash
cp inkcast/secrets.h.example inkcast/secrets.h
```

Fill in your Wi-Fi credentials and, optionally, an OpenWeatherMap key:

```cpp
#define WIFI_SSID     "MyWiFi"
#define WIFI_PASSWORD "password123"
#define OWM_API_KEY   ""
```

`secrets.h` is ignored by git.

### 2. Adjust defaults in `config.h`

Typical first-flash values:

```cpp
#define OWM_LAT              "55.7558"
#define OWM_LON              "37.6176"
#define OWM_CITY             "Moscow"
#define SLEEP_DURATION_MIN   120
```

The weather provider itself is selected at runtime in the admin UI:
- **Open-Meteo** is the default and works without an API key.
- **OpenWeatherMap** requires an active One Call 3.0 key.

Timezone is resolved automatically from the weather API response. `UTC_OFFSET_SEC` is only a fallback before the first successful fetch.

### 3. Build and flash with Arduino IDE

#### Install dependencies

Add the ESP32 board package URL in `File -> Preferences -> Additional Boards Manager URLs`:

```text
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

Then install `esp32 by Espressif Systems` version `2.0.14` or newer.

Required libraries:

| Library | Minimum version | Why it is needed |
|---|---|---|
| `GxEPD2` | 1.5.5+ | Driver for the 3.97" panel |
| `ArduinoJson` | 7.0+ | Parsing Open-Meteo and OWM responses |
| `U8g2_for_Adafruit_GFX` | 1.8.0+ | UTF-8 text rendering over Adafruit GFX |

`Adafruit GFX Library` and `Adafruit BusIO` are pulled in as dependencies.

#### Recommended board settings

Select `ESP32S3 Dev Module` and use these important options:

| Option | Value |
|---|---|
| USB CDC On Boot | `Enabled` |
| Flash Size | `16MB (128Mb)` |
| Partition Scheme | `16M Flash (3MB APP/9.9MB FATFS)` or `Huge APP (3MB No OTA/1MB SPIFFS)` |
| PSRAM | `OPI PSRAM` |

The partition scheme matters. The firmware is too large for the default 1.25 MB app partition because of fonts and weather assets.

#### Upload

1. Connect the board over USB-C.
2. Select the board port in `Tools -> Port`.
3. Open `inkcast/inkcast.ino`.
4. Upload the sketch.
5. Open `Tools -> Serial Monitor` at `115200` baud to inspect boot, Wi-Fi, and screen update logs.

If the serial port does not appear, hold `BOOT`, tap `RESET`, then release `BOOT` to enter the bootloader.

## Web Admin Panel

The onboard admin interface is available only in debug mode.

How to use it:
1. Power the device in debug mode.
2. Open Serial Monitor at `115200` baud.
3. Find the line with the admin URL, usually `http://<ip>/`.
4. Open that address from a browser on the same network.
5. Save changes and let the device reboot.

What can be configured at runtime:
- Weather provider and Open-Meteo forecast model.
- OpenWeatherMap API key.
- Wi-Fi SSID and password.
- Latitude, longitude, and city label.
- Refresh interval.

If saved Wi-Fi settings are broken, the device starts an open access point named `ESP-Weather-Setup`. Connect to it and open `http://192.168.4.1/`.

The admin page is intentionally simple and assumes a trusted home network. If you need access control, add authentication in `admin_server.cpp`.

## Screen Layout

The UI is split into three zones:
- Top status bar with city, date/time, optional admin IP, weather warning marker, Wi-Fi, and battery state.
- Current conditions block with a large icon and key metrics.
- Lower area with a 48-hour chart and 6-day forecast cards.

This layout is tuned for a large-format always-on weather dashboard rather than a minimal numeric display.

## Simulator (`sim/`)

The repository includes a simulator in [`sim/`](sim/) for iterating on layout changes and visual assets without touching the physical device every time. Use it when you want to tweak typography, spacing, icons, or chart composition faster than the flash-test cycle allows.

## Project Structure

```text
assets/   Images for the README and project media
inkcast/  Arduino firmware
sim/      Display simulator for UI iteration
tools/    Supporting scripts and utilities
```

## Power Use

- Default refresh period: every 120 minutes.
- Supported refresh range: 1 to 1440 minutes.
- Between updates the ESP32-S3 enters deep sleep.
- Debug mode disables deep sleep so logs and the admin panel remain available.

This is the main reason the project works well as a battery-powered wall device rather than a permanently tethered display.

## Troubleshooting

- **Sketch too big**: choose a partition scheme with at least a 3 MB app partition.
- **No COM port after connecting USB**: enter the bootloader with `BOOT + RESET`.
- **No deep sleep**: disconnect USB from the computer or remove the debug jumper.
- **Admin panel is missing**: the device is not in debug mode.
- **Wi-Fi credentials changed**: use the `ESP-Weather-Setup` fallback access point.
- **Weather data not loading**: verify coordinates, provider selection, and API key if using OpenWeatherMap.

## Contributing

Contributions are welcome, especially in these areas:
- Hardware variants and wiring notes for other compatible boards.
- UI improvements for the simulator and e-paper layout.
- Power optimization and sleep/wake refinements.
- Documentation, translations, and build workflow improvements.

If you open a PR, include photos, simulator screenshots, or notes about the hardware setup when the change affects the physical device.

## License

See [LICENSE](LICENSE).
