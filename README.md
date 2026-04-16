# CrossPoint Reader — M5Paper S3 Port

Port of the [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) firmware to the **M5Paper S3**.
Built using **PlatformIO** and targeting the **ESP32-S3** (dual-core Xtensa LX7, 240 MHz, 8 MB OPI-PSRAM).

## Release 1.2.1 — CrossPoint PaperS3

### Touch input overhaul

- Restrict touch input to in-book reader and keyboard screens only — all other UI navigation uses footer buttons
- Remove `wasTapped()` tap-to-select from settings, file browser, OPDS browser, WiFi selection, language select, OTA update, clear cache, KOReader auth, QR display, and network mode screens
- Reader sub-activities (menu, chapter selection, footnotes, percent selection, QR, KOReader sync) no longer use full-screen touch zones

### Orientation fix

- Landscape orientation now only applies to in-book reading — all sub-activities (menu, chapter selection, etc.) render in portrait with footer buttons at the physical bottom of the device
- Orientation is automatically restored when returning to the reader from any sub-activity
- Paper S3 orientation options simplified to Portrait and Landscape only

### Upstream ports

- **OPDS search and pagination** (fa2a3d2)
- **Smooth battery percentage reading** (81ae9dd)
- **Manual screen refresh** on power button short press (9c11f3e)
- **Cover + Custom sleep screen** rework (405ce0c)
- **Forward at end of book goes home** (d29b8ee)
- **Slovenian translation** (14ec53a)
- Fix: avoid skipping chapter after screenshot (5c12f2f)
- Fix: use runtime screen dimensions for screenshots (6cd19f5)
- Fix: correct Russian auto-turn and Ukrainian footnote translations (fa3c7d9, cff3e12)
- Fix: missing Swedish translations (57fc655)
- Fix: differential rounding for consistent inter-glyph spacing (1398aeb)
- Fix: back navigation from BMPViewer returns to correct file (8d6b35b)
- Fix: support hyphenation for EPUBs using ISO 639-2 language codes (1c13331)
- Fix: ghosting on exit of BMPViewer (ed54f97)
- Fix: font metrics for combining mark positioning (075ad7d)
- Fix: two small memory leaks in ZipFile (104f391)
- Fix: footnote link text trimming (80772ff)
- Refactor: deduplicate BMP header writing in Xtc (5ba8529)
- Refactor: RAII scoped open/close for ZipFile (b3b43bb)
- Refactor: remove redundant FsFile close() calls (23aad21)
- Refactor: simplify log formatting (c656673)
- Refactor: replace picojpeg cover art conversion with JPEGDEC (40e4c96)
- Chore: drop JPEGDEC patch hook (b898d53)

### Paper S3 specific

- Battery charging indicator and shared drawing helpers
- Stabilize first wifi connection attempt
- Support larger epub metadata caches
- Orient sleep popups for reader screens

## Release 0.2.3

- Hide unsupported Paper S3 orientation options in settings and reader menus
- Normalize saved Paper S3 orientation settings to supported values on load
- Fix Paper S3 chapter selection taps so footer/select actions keep the current row
- `EPD_Painter` remains the display driver
- Long-press chapter skip stays user-controlled in Settings

## Hardware

| | M5Paper S3 |
|---|---|
| **MCU** | ESP32-S3 (dual-core, 240 MHz) |
| **Flash / PSRAM** | 16 MB / 8 MB OPI |
| **Display** | 960×540 parallel e-ink (IT8951) |
| **Input** | GT911 capacitive touch + power button |
| **SD Card** | SPI (CS GPIO47) |
| **Battery** | Li-Po via AXP2101 PMIC |
| **RTC** | BM8563 |

## Features

- EPUB 2/3 parsing and rendering (including images)
- Touch-based navigation (no physical buttons needed)
- File explorer, recent books, and reading progress
- Configurable font, layout, display, and sleep options
- WiFi book upload and OTA updates
- KOReader Sync integration
- Multi-language support
- 2-bit grayscale for cover art and sleep screens

## Building & Flashing

### Prerequisites

- **PlatformIO Core** (`pio`) or **VS Code + PlatformIO IDE**
- USB-C cable
- M5Paper S3

### Clone

```sh
git clone --recursive https://github.com/juicecultus/crosspoint-reader-papers3
cd crosspoint-reader-papers3
```

### Build & Flash

```sh
pio run --target upload
```

### Command line (specific firmware version)

1. Install [`esptool`](https://github.com/espressif/esptool):

   ```sh
   pip install esptool
   ```

2. Download the `firmware.bin` file from the release of your choice via the [releases page](https://github.com/crosspoint-reader/crosspoint-reader/releases).

3. Connect your M5Paper S3 to your computer via USB-C.

4. Note the device location. On Linux, run `dmesg` after connecting. On macOS, run:

   ```sh
   log stream --predicate 'subsystem == "com.apple.iokit"' --info
   ```

5. Flash the firmware:

   ```sh
   esptool.py --chip esp32c3 --port /dev/ttyACM0 --baud 921600 write_flash 0x10000 /path/to/firmware.bin
   ```

   Change `/dev/ttyACM0` to the device for your system.

### Serial Monitor

```sh
pio device monitor
```

## Navigation

The Paper S3 uses a combination of on-screen buttons and touch gestures.

### Footer nav bar (all screens except in-book reader)

Every non-reader screen shows a row of tappable buttons at the bottom:

```
+--------+---------+--------+--------+
|  Back  | Confirm |  Prev  |  Next  |
+--------+---------+--------+--------+
```

| Button | Action |
|--------|--------|
| **Back** | Go back / exit current screen |
| **Confirm** | Select / confirm highlighted item |
| **Prev (▲)** | Previous page of items |
| **Next (▼)** | Next page of items |

Only buttons relevant to the current screen are shown.

### Touch zones (content area)

The content area above the footer is split into three vertical zones:

```
+-----------+-----------+-----------+
|           |           |           |
|   LEFT    |  CENTER   |  RIGHT    |
|   1/3     |   1/3     |   1/3     |
|           |           |           |
+-----------+-----------+-----------+
+--------+---------+--------+--------+
|  Back  | Confirm |  Prev  |  Next  |
+--------+---------+--------+--------+
```

In **long lists** (file browser, chapters, recent books), tapping the content
area selects the item at that position.

### In-book reader (full screen, no footer)

| Zone | Action |
|------|--------|
| **Left** | Previous page |
| **Center** | Open in-book settings menu |
| **Right** | Next page |

### Gestures (in-book reader only)

| Gesture | Action |
|---------|--------|
| **2-finger tap** | Go back / exit reader |
| **Swipe up** | Previous page |
| **Swipe down** | Next page |

## Internals

CrossPoint caches chapter data to the SD card under `.crosspoint/` to reduce RAM usage.

```
.crosspoint/
├── epub_<hash>/
│   ├── progress.bin
│   ├── cover.bmp
│   ├── book.bin
│   └── sections/
│       ├── 0.bin
│       ├── 1.bin
│       └── ...
```

Deleting `.crosspoint/` clears the entire cache.

## Credits

Ported from [crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader) (originally targeting Xteink X4 / ESP32-C3).

Display driver powered by [**EPD_Painter**](https://github.com/tonywestonuk/EPD_Painter) — fast parallel e-ink rendering for ESP32-S3 e-paper displays.

Huge shoutout to [**diy-esp32-epub-reader** by atomic14](https://github.com/atomic14/diy-esp32-epub-reader) for the original inspiration.
