# CrossPoint Reader — M5Paper S3 Fork

Fork of the [CrossPoint Reader S3 port](https://github.com/juicecultus/crosspoint-reader-papers3) of the fork of the [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) targeting the **M5Paper S3**.
Built with **PlatformIO** on **ESP32-S3** (dual-core Xtensa LX7, 240 MHz, 8 MB OPI-PSRAM).

## What's different from upstream

This fork tracks the upstream `crosspoint-reader S3 port` project and fixes some issues in the upstream verion:
- **Touch-tracking menus** — a gray highlight bar follows the finger in list views, with tap-to-select on release
- **Orientation guard** — settings and reader menus hide/normalise unsupported Paper S3 orientations
- **Button remap fix** — `ButtonRemapActivity` disables the footer nav bar while remapping so the rightmost touch zone doesn't trigger "cancel"

and adds M5Paper S3-specific work:
- **To Do list** — read-only checklist from `/todo/todo.md` on the SD card; items can be checked on the device; a companion Python script (`/Documents/PaperS3/todo/sync_todo.py`) syncs check-state between the Mac master file and the SD card
- **MDsupport** - added a read module (activity) to properly render Markdown files. Files with extension .md will be handled by this readerActivity. A list of supported features can be found in  the MDsupport.md file

Everything else mirrors upstream. Files with fork-specific changes are not overwritten during upstream merges.

## Changelog

### Post-0.2.3 (current)

Merged from upstream v1.2.1:

- **Crash report screen** — on panic reboot the device now shows a crash report before returning to the home screen
- **Force Refresh** option added to the short power-button action setting
- **JPEGDEC** library pinned to a specific git commit, removing the patch script
- **ZipFile RAII** — `ScopedOpenClose` guard eliminates all `wasOpen` boilerplate
- **BitmapHelpers** — `BmpRowOrder` enum; `createBmpHeader` now takes explicit row order
- **Xtc cover export** uses `createBmpHeader` (TopDown) instead of manual header bytes
- **Epub Section** cache format bumped to v19 (forces one-time cache regeneration)
- **BookMetadataCache** batch size lookup switched from `vector` to `deque`
- **ChapterHtmlSlimParser** improved footnote link text trimming
- **Slovenian** language added (20 languages total)
- i18n: added `STR_FORCE_REFRESH`, `STR_NEXT_PAGE`, `STR_PREV_PAGE`, `STR_SEARCH`, `STR_CRASH_*`

### 0.2.3

- Hide unsupported Paper S3 orientation options in settings and reader menus
- Normalise saved Paper S3 orientation settings to supported values on load
- Fix chapter-selection taps: footer/select actions preserve the current row
- Long-press chapter skip remains user-controlled in Settings

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
- To Do list with on-device check-off and Mac sync script
- Configurable font, layout, display, and sleep options
- WiFi book upload and OTA updates
- KOReader Sync integration
- Multi-language support (20 languages)
- 2-bit grayscale for cover art and sleep screens
- Crash report screen on unexpected reboot

## Building & Flashing

### Prerequisites

- **PlatformIO Core** (`pio`) or **VS Code + PlatformIO IDE**
- USB-C cable
- M5Paper S3

### Clone

```sh
git clone --recursive <this-repo-url>
cd crosspoint-reader-papers3-fork
```

### Build & Flash

```sh
pio run --target upload
```

### Serial Monitor

```sh
pio device monitor
# or for colour-coded output:
python3 scripts/debugging_monitor.py
```

## Navigation

### Footer nav bar (all screens except the in-book reader)

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
| **Prev (▲)** | Move selection up / previous page |
| **Next (▼)** | Move selection down / next page |

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

In long lists (file browser, chapters, recent books), tapping the content
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

## To Do list

Place a `todo.md` file in the `/todo/` directory on the SD card:

```markdown
# To Do

## Personal
- [ ] Call dentist
- [x] Renew library card

## Work
- [ ] Review project proposal
```

Items can be checked on the device (one-directional — checking only).
Checked state is written back to the SD card when leaving the screen.

### Syncing with Mac

```sh
# Before ejecting the SD card — push Mac master → device
python3 sync_todo.py push /Volumes/SD_CARD/todo/todo.md ~/Documents/PaperS3/todo/todo.md

# After re-inserting the SD card — pull device check-state → Mac master
python3 sync_todo.py pull /Volumes/SD_CARD/todo/todo.md ~/Documents/PaperS3/todo/todo.md
```

The Mac file is authoritative for item text; the device file is authoritative for check state.

## SD card cache

CrossPoint caches chapter data under `.crosspoint/` to reduce RAM usage.

```
.crosspoint/
└── epub_<hash>/
    ├── progress.bin
    ├── cover.bmp
    ├── book.bin
    └── sections/
        ├── 0.bin
        ├── 1.bin
        └── ...
```

Delete `.crosspoint/` to clear the entire cache (all books will be re-indexed on next open).

## Credits

Forked from [crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader) (originally targeting Xteink X4 / ESP32-C3).

Display driver powered by [**EPD_Painter**](https://github.com/tonywestonuk/EPD_Painter).

Original inspiration: [**diy-esp32-epub-reader** by atomic14](https://github.com/atomic14/diy-esp32-epub-reader).
