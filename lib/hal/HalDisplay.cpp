#include <HalDisplay.h>
#include <HalM5Mutex.h>
#include <M5Unified.h>

#include <cstring>

HalDisplay::HalDisplay() : frameBuffer(nullptr) {}

HalDisplay::~HalDisplay() {
  freeGrayscaleBuffers();
  if (frameBuffer) {
    heap_caps_free(frameBuffer);
    frameBuffer = nullptr;
  }
}

void HalDisplay::freeGrayscaleBuffers() {
  if (grayLsbBuffer) { heap_caps_free(grayLsbBuffer); grayLsbBuffer = nullptr; }
  if (grayMsbBuffer) { heap_caps_free(grayMsbBuffer); grayMsbBuffer = nullptr; }
}

void HalDisplay::begin() {
  // Set display to landscape orientation to match our 960x540 framebuffer layout
  // GfxRenderer handles logical portrait→physical landscape rotation internally
  M5.Display.setRotation(1);

  // Disable auto-display so pushSprite() only writes to the EPD buffer.
  // We trigger the single EPD refresh explicitly via M5.Display.display().
  M5.Display.setAutoDisplay(false);

  // Allocate 1-bit framebuffer in PSRAM (64800 bytes for 960x540)
  frameBuffer = static_cast<uint8_t*>(heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!frameBuffer) {
    // Fallback to regular heap if PSRAM not available
    frameBuffer = static_cast<uint8_t*>(malloc(BUFFER_SIZE));
  }
  if (frameBuffer) {
    memset(frameBuffer, 0xFF, BUFFER_SIZE);  // Initialize to white
  }
  if (Serial) Serial.printf("[%lu] HalDisplay: begin() - framebuffer %s (%lu bytes)\n", millis(),
                            frameBuffer ? "allocated" : "FAILED", BUFFER_SIZE);
}

void HalDisplay::clearScreen(uint8_t color) const {
  if (frameBuffer) {
    memset(frameBuffer, color, BUFFER_SIZE);
  }
}

void HalDisplay::drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                           bool fromProgmem) const {
  if (!frameBuffer) return;

  const uint16_t imageWidthBytes = w / 8;

  for (uint16_t row = 0; row < h; row++) {
    const uint16_t destY = y + row;
    if (destY >= DISPLAY_HEIGHT) break;

    const uint16_t destOffset = destY * DISPLAY_WIDTH_BYTES + (x / 8);
    const uint16_t srcOffset = row * imageWidthBytes;

    for (uint16_t col = 0; col < imageWidthBytes; col++) {
      if ((x / 8 + col) >= DISPLAY_WIDTH_BYTES) break;

      if (fromProgmem) {
        frameBuffer[destOffset + col] = pgm_read_byte(&imageData[srcOffset + col]);
      } else {
        frameBuffer[destOffset + col] = imageData[srcOffset + col];
      }
    }
  }
}

void HalDisplay::drawImageTransparent(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                      bool fromProgmem) const {
  if (!frameBuffer) return;

  const uint16_t imageWidthBytes = w / 8;

  for (uint16_t row = 0; row < h; row++) {
    const uint16_t destY = y + row;
    if (destY >= DISPLAY_HEIGHT) break;

    const uint16_t destOffset = destY * DISPLAY_WIDTH_BYTES + (x / 8);
    const uint16_t srcOffset = row * imageWidthBytes;

    for (uint16_t col = 0; col < imageWidthBytes; col++) {
      if ((x / 8 + col) >= DISPLAY_WIDTH_BYTES) break;

      uint8_t srcByte = fromProgmem ? pgm_read_byte(&imageData[srcOffset + col]) : imageData[srcOffset + col];
      frameBuffer[destOffset + col] &= srcByte;
    }
  }
}

void HalDisplay::pushFrameBufferToDisplay() {
  if (!frameBuffer) return;

  // Use M5Canvas (sprite) to push our 1-bit framebuffer to the EPD
  // Our format: 1=white, 0=black, MSB first, packed 8 pixels per byte
  // M5Canvas's 1-bit depth uses the same MSB-first packing
  HalM5Mutex::lock();

  M5Canvas canvas(&M5.Display);
  canvas.setColorDepth(1);
  canvas.setPaletteColor(0, TFT_BLACK);
  canvas.setPaletteColor(1, TFT_WHITE);
  canvas.createSprite(DISPLAY_WIDTH, DISPLAY_HEIGHT);

  if (canvas.getBuffer()) {
    memcpy(canvas.getBuffer(), frameBuffer, BUFFER_SIZE);
    canvas.pushSprite(0, 0);
  }

  canvas.deleteSprite();

  HalM5Mutex::unlock();
}

void HalDisplay::displayBuffer(RefreshMode mode, bool turnOffScreen) {
  pushFrameBufferToDisplay();

  // Trigger EPD refresh via M5GFX
  HalM5Mutex::lock();
  M5.Display.display();
  HalM5Mutex::unlock();
}

void HalDisplay::refreshDisplay(RefreshMode mode, bool turnOffScreen) {
  HalM5Mutex::lock();
  M5.Display.display();
  HalM5Mutex::unlock();
}

void HalDisplay::deepSleep() {
  HalM5Mutex::lock();
  M5.Display.sleep();
  HalM5Mutex::unlock();
}

uint8_t* HalDisplay::getFrameBuffer() const { return frameBuffer; }

void HalDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  // For grayscale on PaperS3, combine LSB+MSB planes into a 2-bit grayscale image
  // Use M5Canvas with 2-bit color depth and a grayscale palette
  if (!lsbBuffer || !msbBuffer) return;

  HalM5Mutex::lock();

  M5Canvas canvas(&M5.Display);
  canvas.setColorDepth(2);
  // 2-bit palette: 4 gray levels (0=black, 1=dark gray, 2=light gray, 3=white)
  canvas.setPaletteColor(0, 0x000000);  // black
  canvas.setPaletteColor(1, 0x555555);  // dark gray
  canvas.setPaletteColor(2, 0xAAAAAA);  // light gray
  canvas.setPaletteColor(3, 0xFFFFFF);  // white
  canvas.createSprite(DISPLAY_WIDTH, DISPLAY_HEIGHT);

  if (canvas.getBuffer()) {
    uint8_t* dst = static_cast<uint8_t*>(canvas.getBuffer());
    // 2-bit packed format: 4 pixels per byte, MSB first
    // Pixel 0 in bits 7-6, pixel 1 in bits 5-4, pixel 2 in bits 3-2, pixel 3 in bits 1-0
    for (uint16_t y = 0; y < DISPLAY_HEIGHT; y++) {
      const uint8_t* lsbRow = &lsbBuffer[y * DISPLAY_WIDTH_BYTES];
      const uint8_t* msbRow = &msbBuffer[y * DISPLAY_WIDTH_BYTES];

      for (uint16_t byteIdx = 0; byteIdx < DISPLAY_WIDTH_BYTES; byteIdx++) {
        uint8_t lsb = lsbRow[byteIdx];
        uint8_t msb = msbRow[byteIdx];

        // Each source byte has 8 pixels; pack into 2 destination bytes (4 pixels each)
        uint8_t dstByte0 = 0, dstByte1 = 0;
        for (int i = 0; i < 4; i++) {
          int bit = 7 - i;
          uint8_t gray = (((msb >> bit) & 1) << 1) | ((lsb >> bit) & 1);
          dstByte0 |= (gray << (6 - i * 2));
        }
        for (int i = 0; i < 4; i++) {
          int bit = 3 - i;
          uint8_t gray = (((msb >> bit) & 1) << 1) | ((lsb >> bit) & 1);
          dstByte1 |= (gray << (6 - i * 2));
        }
        size_t dstOffset = y * (DISPLAY_WIDTH / 4) + byteIdx * 2;
        dst[dstOffset] = dstByte0;
        dst[dstOffset + 1] = dstByte1;
      }
    }

    canvas.pushSprite(0, 0);
  }

  canvas.deleteSprite();

  HalM5Mutex::unlock();
}

void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) {
  if (!lsbBuffer) return;
  if (!grayLsbBuffer) {
    grayLsbBuffer = static_cast<uint8_t*>(heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!grayLsbBuffer) grayLsbBuffer = static_cast<uint8_t*>(malloc(BUFFER_SIZE));
  }
  if (grayLsbBuffer) memcpy(grayLsbBuffer, lsbBuffer, BUFFER_SIZE);
}

void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) {
  if (!msbBuffer) return;
  if (!grayMsbBuffer) {
    grayMsbBuffer = static_cast<uint8_t*>(heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!grayMsbBuffer) grayMsbBuffer = static_cast<uint8_t*>(malloc(BUFFER_SIZE));
  }
  if (grayMsbBuffer) memcpy(grayMsbBuffer, msbBuffer, BUFFER_SIZE);
}

void HalDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) {
  // After grayscale display, restore BW mode
  // Push the BW buffer back to prepare for next fast refresh
  if (bwBuffer) {
    pushFrameBufferToDisplay();
  }
}

void HalDisplay::displayGrayBuffer(bool turnOffScreen) {
  if (grayLsbBuffer && grayMsbBuffer) {
    // Combine stored LSB+MSB planes into grayscale and push to display
    copyGrayscaleBuffers(grayLsbBuffer, grayMsbBuffer);
    freeGrayscaleBuffers();
  }
  HalM5Mutex::lock();
  M5.Display.display();
  HalM5Mutex::unlock();
}
