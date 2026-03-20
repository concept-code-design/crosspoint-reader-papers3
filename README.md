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

The Paper S3 uses touch gestures for all navigation. The screen is split into
three equal vertical zones, and additional gestures handle going back and
scrolling through long lists.

### Gestures (work everywhere)

| Gesture | Action |
|---------|--------|
| **2-finger tap** | Go back / exit current screen |
| **Swipe up** | Page backward (lists) or previous page (reader) |
| **Swipe down** | Page forward (lists) or next page (reader) |

### Touch zones

```
+-----------+-----------+-----------+
|           |           |           |
|   LEFT    |  CENTER   |  RIGHT    |
|   1/3     |   1/3     |   1/3     |
|           |           |           |
+-----------+-----------+-----------+
```

### In-book reader

| Zone | Action |
|------|--------|
| **Left** | Previous page |
| **Center** | Open in-book settings menu |
| **Right** | Next page |

### Menus and settings

| Zone | Action |
|------|--------|
| **Left** | Move selector up |
| **Center** | Select / confirm |
| **Right** | Move selector down |

### Long lists (file browser, chapters, recent books)

| Zone | Action |
|------|--------|
| **Left** | Previous page of items |
| **Center** | Tap to select item by position |
| **Right** | Next page of items |

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
