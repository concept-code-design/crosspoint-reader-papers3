# Changelog

## Unreleased

### Upstream merge (crosspoint-reader v1.2.1)

- **Crash report screen** — on panic reboot the device navigates to a new `CrashActivity` that displays the crash reason before returning home; `clearPanic()` is now called inside the activity instead of immediately at boot
- **Force Refresh** — new `FORCE_REFRESH` option in the short power-button action setting triggers a half-refresh of the e-ink screen on demand
- **JPEGDEC** library replaced: `bitbank2/JPEGDEC @ ^1.8.0` + patch script swapped for a pinned git commit (`#86282979`) that requires no patching; file-based JPEG decoding now uses JPEGDEC on all paths (picojpeg removed)
- **ZipFile RAII** — `ScopedOpenClose` guard eliminates the manual `wasOpen`/`close` pattern in all six public methods; `fillUncompressedSizes` switched from `std::vector` to `std::deque`
- **BitmapHelpers** — `BmpRowOrder` enum (`BottomUp` / `TopDown`) added; `createBmpHeader` now takes an explicit row-order argument; callers updated (`ScreenshotUtil`, `Xtc`)
- **Xtc cover export** — replaced ~40-line manual BMP header with a single `createBmpHeader(..., BmpRowOrder::TopDown)` call
- **Epub Section** cache format version bumped 18 → 19 (forces one-time cache regeneration on next book open)
- **BookMetadataCache** batch uncompressed-size lookup switched from `std::vector` to `std::deque`
- **ChapterHtmlSlimParser** improved footnote link text trimming (strips leading/trailing whitespace and brackets in a single pass)
- **Epub.cpp** minor file close ordering fix
- **Slovenian** language added (20 languages total)
- **i18n**: added `STR_FORCE_REFRESH`, `STR_NEXT_PAGE`, `STR_PREV_PAGE`, `STR_SEARCH`, `STR_CRASH_TITLE`, `STR_CRASH_DESCRIPTION`, `STR_CRASH_REASON`, `STR_CRASH_NO_REASON`

### Calendar

- **Calendar menu entry** — new item in the home menu (below To Do) that opens `/calendar/today.md` from the SD card via `MdReaderActivity`; uses `CalIcon` (32×32 from `calendar.hls.h`)

### RTC / Time

- **HalRTC** — new HAL class (`lib/hal/HalRTC.h/.cpp`) wraps the BM8563 RTC (PCF8563-compatible) on I2C1 (SDA=41, SCL=42); `begin()` reads the stored time and sets the ESP32 system clock at boot; `syncWithNTP()` polls an NTP server (default `ntp0.nl.net`) and writes the result back to the BM8563
- **NTP sync on WiFi connect** — `CrossPointWebServerActivity` calls `halRTC.syncWithNTP()` immediately after a successful WiFi connection, keeping the RTC accurate whenever the device goes online

### To Do list

- **TodoActivity** — new screen that reads `/todo/todo.md` from the SD card, displays items grouped by `##` section headings, and lets the user check off items (one-directional); checked state is written back on exit
- **sync_todo.py** — Mac-side Python script that merges the Mac master item list with device check-states (`push`) or pulls device completions back into the Mac file (`pull`); item matching is by normalised text
- **Icon tooling** — `convert_icon.py` converts an editable `#`/`.` pixel grid (32×32) back to a C `uint8_t[]` header; editable sources: `todo_icon_edit.txt` → `todo.h`, `todo2_icon_edit.txt` → `todo2.h`

## 0.2.3

### UI / UX
- **Vertical text centering** in all list rows (file browser, settings, recent books) — text, icons, subtitles, and values are now properly centered within their row height using dynamic font metrics instead of hardcoded pixel offsets
- **Tap-friendly row heights** for in-book menus on PaperS3: chapter selection, footnotes, and reader menu rows increased from 30–36px to 75px with vertically centered text
- **Lyra 3-Covers theme PaperS3 fix** — added PaperS3-specific metrics (120px menu/list rows, 16px spacing) so touch hit-testing matches the rendered layout; previously all taps mapped to wrong items due to metrics mismatch (64px vs 120px)
- **Multi-cover touch selection** — tapping a specific book cover in the 3-cover home layout now opens that book instead of always opening the first one (uses touch X coordinate)
- **Boot splash footer** aligned with in-app status bar position and pushed 2px higher to prevent descender ghosting (letters like j, g)
- **Reader status bar footer** pushed 2px higher to avoid descender mirroring at the screen edge
- **First-open cover skip** — books now skip the cover page on first open; uses a proper `isFirstOpen` flag (progress.bin existence) instead of the unreliable `spineIndex == 0` check, with fallback to spine 1 when no text reference is found

### Rendering
- **Force full e-ink refresh** on every activity transition (PaperS3) to eliminate ghosting artifacts
- **VIEWABLE_MARGIN_BOTTOM** reduced from 22 to 16 to reclaim empty space under the footer

### Performance / Stability
- **JPEGDEC stack overflow fix** — the ~16KB JPEGDEC object is now heap-allocated in PSRAM via placement new instead of overflowing the 8KB render task stack
- **Fast JPEG thumbnail path** — on PaperS3, cover thumbnails are decoded directly from PSRAM using JPEGDEC at 1/8 scale with Floyd-Steinberg dithering, bypassing the slow picojpeg + temp file path
- **JPEGDEC patches** for progressive JPEG support (skip AC Huffman tables) and MCU_SKIP guard to prevent crashes on grayscale chroma skip
- **Build optimization** — `-O2` with `-Os` removed from build_unflags so the speed optimization actually takes effect

### CI / Release
- **GitHub Actions**: fixed `upload-artifact@v6` → `@v4` in CI workflow (v6 doesn't exist)
- **GitHub Release creation**: release workflow now creates a GitHub Release with firmware binaries attached via `softprops/action-gh-release@v2`

### Credits
- Display driver: [EPD_Painter](https://github.com/nickoala/EPD_Painter) by nickoala
