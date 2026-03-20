# CrossPoint Reader — M5Paper S3 Port

Port of the [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) firmware to the **M5Paper S3**.
Built using **PlatformIO** and targeting the **ESP32-S3** (dual-core Xtensa LX7, 240 MHz, 8 MB OPI-PSRAM).

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

### Gestures (work everywhere)

| Gesture | Action |
|---------|--------|
| **2-finger tap** | Go back / exit current screen |
| **Swipe up** | Previous page (lists or reader) |
| **Swipe down** | Next page (lists or reader) |

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

Display driver powered by [**EPD_Painter**](https://github.com/nickoala/EPD_Painter) — fast parallel e-ink rendering for IT8951-based displays on ESP32-S3.

Huge shoutout to [**diy-esp32-epub-reader** by atomic14](https://github.com/atomic14/diy-esp32-epub-reader) for the original inspiration.
