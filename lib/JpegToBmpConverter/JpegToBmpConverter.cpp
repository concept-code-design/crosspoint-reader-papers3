#include "JpegToBmpConverter.h"

#include <HalStorage.h>
#include <Logging.h>
#include <picojpeg.h>

#include <cstdio>
#include <cstring>
#include <new>

#include "BitmapHelpers.h"

// Context structure for picojpeg callback
struct JpegReadContext {
  FsFile& file;
  uint8_t buffer[512];
  size_t bufferPos;
  size_t bufferFilled;
};

// ============================================================================
// IMAGE PROCESSING OPTIONS - Toggle these to test different configurations
// ============================================================================
constexpr bool USE_8BIT_OUTPUT = false;  // true: 8-bit grayscale (no quantization), false: 2-bit (4 levels)
// Dithering method selection (only one should be true, or all false for simple quantization):
constexpr bool USE_ATKINSON = true;          // Atkinson dithering (cleaner than F-S, less error diffusion)
constexpr bool USE_FLOYD_STEINBERG = false;  // Floyd-Steinberg error diffusion (can cause "worm" artifacts)
constexpr bool USE_NOISE_DITHERING = false;  // Hash-based noise dithering (good for downsampling)
// Pre-resize to target display size (CRITICAL: avoids dithering artifacts from post-downsampling)
constexpr bool USE_PRESCALE = true;  // true: scale image to target size before dithering
#if CROSSPOINT_PAPERS3
constexpr int TARGET_MAX_WIDTH = 540;   // Paper S3 portrait width (must match display)
constexpr int TARGET_MAX_HEIGHT = 960;  // Paper S3 portrait height (must match display)
#else
constexpr int TARGET_MAX_WIDTH = 480;   // X4 portrait display width
constexpr int TARGET_MAX_HEIGHT = 800;  // X4 portrait display height
#endif
// ============================================================================

inline void write16(Print& out, const uint16_t value) {
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
}

inline void write32(Print& out, const uint32_t value) {
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
  out.write((value >> 16) & 0xFF);
  out.write((value >> 24) & 0xFF);
}

inline void write32Signed(Print& out, const int32_t value) {
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
  out.write((value >> 16) & 0xFF);
  out.write((value >> 24) & 0xFF);
}

// Helper function: Write BMP header with 8-bit grayscale (256 levels)
void writeBmpHeader8bit(Print& bmpOut, const int width, const int height) {
  // Calculate row padding (each row must be multiple of 4 bytes)
  const int bytesPerRow = (width + 3) / 4 * 4;  // 8 bits per pixel, padded
  const int imageSize = bytesPerRow * height;
  const uint32_t paletteSize = 256 * 4;  // 256 colors * 4 bytes (BGRA)
  const uint32_t fileSize = 14 + 40 + paletteSize + imageSize;

  // BMP File Header (14 bytes)
  bmpOut.write('B');
  bmpOut.write('M');
  write32(bmpOut, fileSize);
  write32(bmpOut, 0);                      // Reserved
  write32(bmpOut, 14 + 40 + paletteSize);  // Offset to pixel data

  // DIB Header (BITMAPINFOHEADER - 40 bytes)
  write32(bmpOut, 40);
  write32Signed(bmpOut, width);
  write32Signed(bmpOut, -height);  // Negative height = top-down bitmap
  write16(bmpOut, 1);              // Color planes
  write16(bmpOut, 8);              // Bits per pixel (8 bits)
  write32(bmpOut, 0);              // BI_RGB (no compression)
  write32(bmpOut, imageSize);
  write32(bmpOut, 2835);  // xPixelsPerMeter (72 DPI)
  write32(bmpOut, 2835);  // yPixelsPerMeter (72 DPI)
  write32(bmpOut, 256);   // colorsUsed
  write32(bmpOut, 256);   // colorsImportant

  // Color Palette (256 grayscale entries x 4 bytes = 1024 bytes)
  for (int i = 0; i < 256; i++) {
    bmpOut.write(static_cast<uint8_t>(i));  // Blue
    bmpOut.write(static_cast<uint8_t>(i));  // Green
    bmpOut.write(static_cast<uint8_t>(i));  // Red
    bmpOut.write(static_cast<uint8_t>(0));  // Reserved
  }
}

// Helper function: Write BMP header with 1-bit color depth (black and white)
static void writeBmpHeader1bit(Print& bmpOut, const int width, const int height) {
  // Calculate row padding (each row must be multiple of 4 bytes)
  const int bytesPerRow = (width + 31) / 32 * 4;  // 1 bit per pixel, round up to 4-byte boundary
  const int imageSize = bytesPerRow * height;
  const uint32_t fileSize = 62 + imageSize;  // 14 (file header) + 40 (DIB header) + 8 (palette) + image

  // BMP File Header (14 bytes)
  bmpOut.write('B');
  bmpOut.write('M');
  write32(bmpOut, fileSize);  // File size
  write32(bmpOut, 0);         // Reserved
  write32(bmpOut, 62);        // Offset to pixel data (14 + 40 + 8)

  // DIB Header (BITMAPINFOHEADER - 40 bytes)
  write32(bmpOut, 40);
  write32Signed(bmpOut, width);
  write32Signed(bmpOut, -height);  // Negative height = top-down bitmap
  write16(bmpOut, 1);              // Color planes
  write16(bmpOut, 1);              // Bits per pixel (1 bit)
  write32(bmpOut, 0);              // BI_RGB (no compression)
  write32(bmpOut, imageSize);
  write32(bmpOut, 2835);  // xPixelsPerMeter (72 DPI)
  write32(bmpOut, 2835);  // yPixelsPerMeter (72 DPI)
  write32(bmpOut, 2);     // colorsUsed
  write32(bmpOut, 2);     // colorsImportant

  // Color Palette (2 colors x 4 bytes = 8 bytes)
  // Format: Blue, Green, Red, Reserved (BGRA)
  // Note: In 1-bit BMP, palette index 0 = black, 1 = white
  uint8_t palette[8] = {
      0x00, 0x00, 0x00, 0x00,  // Color 0: Black
      0xFF, 0xFF, 0xFF, 0x00   // Color 1: White
  };
  for (const uint8_t i : palette) {
    bmpOut.write(i);
  }
}

// Helper function: Write BMP header with 2-bit color depth
static void writeBmpHeader2bit(Print& bmpOut, const int width, const int height) {
  // Calculate row padding (each row must be multiple of 4 bytes)
  const int bytesPerRow = (width * 2 + 31) / 32 * 4;  // 2 bits per pixel, round up
  const int imageSize = bytesPerRow * height;
  const uint32_t fileSize = 70 + imageSize;  // 14 (file header) + 40 (DIB header) + 16 (palette) + image

  // BMP File Header (14 bytes)
  bmpOut.write('B');
  bmpOut.write('M');
  write32(bmpOut, fileSize);  // File size
  write32(bmpOut, 0);         // Reserved
  write32(bmpOut, 70);        // Offset to pixel data

  // DIB Header (BITMAPINFOHEADER - 40 bytes)
  write32(bmpOut, 40);
  write32Signed(bmpOut, width);
  write32Signed(bmpOut, -height);  // Negative height = top-down bitmap
  write16(bmpOut, 1);              // Color planes
  write16(bmpOut, 2);              // Bits per pixel (2 bits)
  write32(bmpOut, 0);              // BI_RGB (no compression)
  write32(bmpOut, imageSize);
  write32(bmpOut, 2835);  // xPixelsPerMeter (72 DPI)
  write32(bmpOut, 2835);  // yPixelsPerMeter (72 DPI)
  write32(bmpOut, 4);     // colorsUsed
  write32(bmpOut, 4);     // colorsImportant

  // Color Palette (4 colors x 4 bytes = 16 bytes)
  // Format: Blue, Green, Red, Reserved (BGRA)
  uint8_t palette[16] = {
      0x00, 0x00, 0x00, 0x00,  // Color 0: Black
      0x55, 0x55, 0x55, 0x00,  // Color 1: Dark gray (85)
      0xAA, 0xAA, 0xAA, 0x00,  // Color 2: Light gray (170)
      0xFF, 0xFF, 0xFF, 0x00   // Color 3: White
  };
  for (const uint8_t i : palette) {
    bmpOut.write(i);
  }
}

// Callback function for picojpeg to read JPEG data
unsigned char JpegToBmpConverter::jpegReadCallback(unsigned char* pBuf, const unsigned char buf_size,
                                                   unsigned char* pBytes_actually_read, void* pCallback_data) {
  auto* context = static_cast<JpegReadContext*>(pCallback_data);

  if (!context || !context->file) {
    return PJPG_STREAM_READ_ERROR;
  }

  // Check if we need to refill our context buffer
  if (context->bufferPos >= context->bufferFilled) {
    context->bufferFilled = context->file.read(context->buffer, sizeof(context->buffer));
    context->bufferPos = 0;

    if (context->bufferFilled == 0) {
      // EOF or error
      *pBytes_actually_read = 0;
      return 0;  // Success (EOF is normal)
    }
  }

  // Copy available bytes to picojpeg's buffer
  const size_t available = context->bufferFilled - context->bufferPos;
  const size_t toRead = available < buf_size ? available : buf_size;

  memcpy(pBuf, context->buffer + context->bufferPos, toRead);
  context->bufferPos += toRead;
  *pBytes_actually_read = static_cast<unsigned char>(toRead);

  return 0;  // Success
}

// Internal implementation with configurable target size and bit depth
bool JpegToBmpConverter::jpegFileToBmpStreamInternal(FsFile& jpegFile, Print& bmpOut, int targetWidth, int targetHeight,
                                                     bool oneBit, bool crop) {
  LOG_DBG("JPG", "Converting JPEG to %s BMP (target: %dx%d)", oneBit ? "1-bit" : "2-bit", targetWidth, targetHeight);

  // Setup context for picojpeg callback
  JpegReadContext context = {.file = jpegFile, .bufferPos = 0, .bufferFilled = 0};

  // Initialize picojpeg decoder
  pjpeg_image_info_t imageInfo;
  const unsigned char status = pjpeg_decode_init(&imageInfo, jpegReadCallback, &context, 0);
  if (status != 0) {
    LOG_ERR("JPG", "JPEG decode init failed with error code: %d", status);
    return false;
  }

  LOG_DBG("JPG", "JPEG dimensions: %dx%d, components: %d, MCUs: %dx%d", imageInfo.m_width, imageInfo.m_height,
          imageInfo.m_comps, imageInfo.m_MCUSPerRow, imageInfo.m_MCUSPerCol);

  // Safety limits to prevent memory issues on ESP32
  constexpr int MAX_IMAGE_WIDTH = 2048;
  constexpr int MAX_IMAGE_HEIGHT = 3072;
  constexpr int MAX_MCU_ROW_BYTES = 65536;

  if (imageInfo.m_width > MAX_IMAGE_WIDTH || imageInfo.m_height > MAX_IMAGE_HEIGHT) {
    LOG_DBG("JPG", "Image too large (%dx%d), max supported: %dx%d", imageInfo.m_width, imageInfo.m_height,
            MAX_IMAGE_WIDTH, MAX_IMAGE_HEIGHT);
    return false;
  }

  // Calculate output dimensions (pre-scale to fit display exactly)
  int outWidth = imageInfo.m_width;
  int outHeight = imageInfo.m_height;
  // Use fixed-point scaling (16.16) for sub-pixel accuracy
  uint32_t scaleX_fp = 65536;  // 1.0 in 16.16 fixed point
  uint32_t scaleY_fp = 65536;
  bool needsScaling = false;

  if (targetWidth > 0 && targetHeight > 0 && (imageInfo.m_width != targetWidth || imageInfo.m_height != targetHeight)) {
    // Calculate scale to fit/fill target dimensions while maintaining aspect ratio
    const float scaleToFitWidth = static_cast<float>(targetWidth) / imageInfo.m_width;
    const float scaleToFitHeight = static_cast<float>(targetHeight) / imageInfo.m_height;
    // We scale to the smaller dimension, so we can potentially crop later.
    float scale = 1.0;
    if (crop) {  // if we will crop, scale to the smaller dimension
      scale = (scaleToFitWidth > scaleToFitHeight) ? scaleToFitWidth : scaleToFitHeight;
    } else {  // else, scale to the larger dimension to fit
      scale = (scaleToFitWidth < scaleToFitHeight) ? scaleToFitWidth : scaleToFitHeight;
    }

    outWidth = static_cast<int>(imageInfo.m_width * scale);
    outHeight = static_cast<int>(imageInfo.m_height * scale);

    // Ensure at least 1 pixel
    if (outWidth < 1) outWidth = 1;
    if (outHeight < 1) outHeight = 1;

    // Calculate fixed-point scale factors (source pixels per output pixel)
    // scaleX_fp = (srcWidth << 16) / outWidth
    scaleX_fp = (static_cast<uint32_t>(imageInfo.m_width) << 16) / outWidth;
    scaleY_fp = (static_cast<uint32_t>(imageInfo.m_height) << 16) / outHeight;
    needsScaling = true;

    LOG_DBG("JPG", "Scaling %dx%d -> %dx%d (target %dx%d)", imageInfo.m_width, imageInfo.m_height, outWidth, outHeight,
            targetWidth, targetHeight);
  }

  // Write BMP header with output dimensions
  int bytesPerRow;
  if (USE_8BIT_OUTPUT && !oneBit) {
    writeBmpHeader8bit(bmpOut, outWidth, outHeight);
    bytesPerRow = (outWidth + 3) / 4 * 4;
  } else if (oneBit) {
    writeBmpHeader1bit(bmpOut, outWidth, outHeight);
    bytesPerRow = (outWidth + 31) / 32 * 4;  // 1 bit per pixel
  } else {
    writeBmpHeader2bit(bmpOut, outWidth, outHeight);
    bytesPerRow = (outWidth * 2 + 31) / 32 * 4;
  }

  uint8_t* rowBuffer = nullptr;
  uint8_t* mcuRowBuffer = nullptr;
  AtkinsonDitherer* atkinsonDitherer = nullptr;
  FloydSteinbergDitherer* fsDitherer = nullptr;
  Atkinson1BitDitherer* atkinson1BitDitherer = nullptr;
  uint32_t* rowAccum = nullptr;  // Accumulator for each output X (32-bit for larger sums)
  uint32_t* rowCount = nullptr;  // Count of source pixels accumulated per output X

  // RAII guard: frees all heap resources on any return path, including early exits.
  // Holds references so it always sees the latest pointer values assigned below.
  struct Cleanup {
    uint8_t*& rowBuffer;
    uint8_t*& mcuRowBuffer;
    AtkinsonDitherer*& atkinsonDitherer;
    FloydSteinbergDitherer*& fsDitherer;
    Atkinson1BitDitherer*& atkinson1BitDitherer;
    uint32_t*& rowAccum;
    uint32_t*& rowCount;
    ~Cleanup() {
      delete[] rowAccum;
      delete[] rowCount;
      delete atkinsonDitherer;
      delete fsDitherer;
      delete atkinson1BitDitherer;
      free(mcuRowBuffer);
      free(rowBuffer);
    }
  } cleanup{rowBuffer, mcuRowBuffer, atkinsonDitherer, fsDitherer, atkinson1BitDitherer, rowAccum, rowCount};

  // Allocate row buffer
  rowBuffer = static_cast<uint8_t*>(malloc(bytesPerRow));
  if (!rowBuffer) {
    LOG_ERR("JPG", "Failed to allocate row buffer");
    return false;
  }

  // Allocate a buffer for one MCU row worth of grayscale pixels
  // This is the minimal memory needed for streaming conversion
  const int mcuPixelHeight = imageInfo.m_MCUHeight;
  const int mcuRowPixels = imageInfo.m_width * mcuPixelHeight;

  // Validate MCU row buffer size before allocation
  if (mcuRowPixels > MAX_MCU_ROW_BYTES) {
    LOG_DBG("JPG", "MCU row buffer too large (%d bytes), max: %d", mcuRowPixels, MAX_MCU_ROW_BYTES);
    return false;
  }

  mcuRowBuffer = static_cast<uint8_t*>(malloc(mcuRowPixels));
  if (!mcuRowBuffer) {
    LOG_ERR("JPG", "Failed to allocate MCU row buffer (%d bytes)", mcuRowPixels);
    return false;
  }

  // Create ditherer if enabled
  // Use OUTPUT dimensions for dithering (after prescaling)
  if (oneBit) {
    // For 1-bit output, use Atkinson dithering for better quality
    atkinson1BitDitherer = new Atkinson1BitDitherer(outWidth);
  } else if (!USE_8BIT_OUTPUT) {
    if (USE_ATKINSON) {
      atkinsonDitherer = new AtkinsonDitherer(outWidth);
    } else if (USE_FLOYD_STEINBERG) {
      fsDitherer = new FloydSteinbergDitherer(outWidth);
    }
  }

  // For scaling: accumulate source rows into scaled output rows
  // We need to track which source Y maps to which output Y
  // Using fixed-point: srcY_fp = outY * scaleY_fp (gives source Y in 16.16 format)
  int currentOutY = 0;             // Current output row being accumulated
  uint32_t nextOutY_srcStart = 0;  // Source Y where next output row starts (16.16 fixed point)

  if (needsScaling) {
    rowAccum = new uint32_t[outWidth]();
    rowCount = new uint32_t[outWidth]();
    nextOutY_srcStart = scaleY_fp;  // First boundary is at scaleY_fp (source Y for outY=1)
  }

  // Process MCUs row-by-row and write to BMP as we go (top-down)
  const int mcuPixelWidth = imageInfo.m_MCUWidth;

  for (int mcuY = 0; mcuY < imageInfo.m_MCUSPerCol; mcuY++) {
    // Clear the MCU row buffer
    memset(mcuRowBuffer, 0, mcuRowPixels);

    // Decode one row of MCUs
    for (int mcuX = 0; mcuX < imageInfo.m_MCUSPerRow; mcuX++) {
      const unsigned char mcuStatus = pjpeg_decode_mcu();
      if (mcuStatus != 0) {
        if (mcuStatus == PJPG_NO_MORE_BLOCKS) {
          LOG_ERR("JPG", "Unexpected end of blocks at MCU (%d, %d)", mcuX, mcuY);
        } else {
          LOG_ERR("JPG", "JPEG decode MCU failed at (%d, %d) with error code: %d", mcuX, mcuY, mcuStatus);
        }
        return false;
      }

      // picojpeg stores MCU data in 8x8 blocks
      // Block layout: H2V2(16x16)=0,64,128,192 H2V1(16x8)=0,64 H1V2(8x16)=0,128
      for (int blockY = 0; blockY < mcuPixelHeight; blockY++) {
        for (int blockX = 0; blockX < mcuPixelWidth; blockX++) {
          const int pixelX = mcuX * mcuPixelWidth + blockX;
          if (pixelX >= imageInfo.m_width) continue;

          // Calculate proper block offset for picojpeg buffer
          const int blockCol = blockX / 8;
          const int blockRow = blockY / 8;
          const int localX = blockX % 8;
          const int localY = blockY % 8;
          const int blocksPerRow = mcuPixelWidth / 8;
          const int blockIndex = blockRow * blocksPerRow + blockCol;
          const int pixelOffset = blockIndex * 64 + localY * 8 + localX;

          uint8_t gray;
          if (imageInfo.m_comps == 1) {
            gray = imageInfo.m_pMCUBufR[pixelOffset];
          } else {
            const uint8_t r = imageInfo.m_pMCUBufR[pixelOffset];
            const uint8_t g = imageInfo.m_pMCUBufG[pixelOffset];
            const uint8_t b = imageInfo.m_pMCUBufB[pixelOffset];
            gray = (r * 25 + g * 50 + b * 25) / 100;
          }

          mcuRowBuffer[blockY * imageInfo.m_width + pixelX] = gray;
        }
      }
    }

    // Process source rows from this MCU row
    const int startRow = mcuY * mcuPixelHeight;
    const int endRow = (mcuY + 1) * mcuPixelHeight;

    for (int y = startRow; y < endRow && y < imageInfo.m_height; y++) {
      const int bufferY = y - startRow;

      if (!needsScaling) {
        // No scaling - direct output (1:1 mapping)
        memset(rowBuffer, 0, bytesPerRow);

        if (USE_8BIT_OUTPUT && !oneBit) {
          for (int x = 0; x < outWidth; x++) {
            const uint8_t gray = mcuRowBuffer[bufferY * imageInfo.m_width + x];
            rowBuffer[x] = adjustPixel(gray);
          }
        } else if (oneBit) {
          // 1-bit output with Atkinson dithering for better quality
          for (int x = 0; x < outWidth; x++) {
            const uint8_t gray = mcuRowBuffer[bufferY * imageInfo.m_width + x];
            const uint8_t bit =
                atkinson1BitDitherer ? atkinson1BitDitherer->processPixel(gray, x) : quantize1bit(gray, x, y);
            // Pack 1-bit value: MSB first, 8 pixels per byte
            const int byteIndex = x / 8;
            const int bitOffset = 7 - (x % 8);
            rowBuffer[byteIndex] |= (bit << bitOffset);
          }
          if (atkinson1BitDitherer) atkinson1BitDitherer->nextRow();
        } else {
          // 2-bit output
          for (int x = 0; x < outWidth; x++) {
            const uint8_t gray = adjustPixel(mcuRowBuffer[bufferY * imageInfo.m_width + x]);
            uint8_t twoBit;
            if (atkinsonDitherer) {
              twoBit = atkinsonDitherer->processPixel(gray, x);
            } else if (fsDitherer) {
              twoBit = fsDitherer->processPixel(gray, x);
            } else {
              twoBit = quantize(gray, x, y);
            }
            const int byteIndex = (x * 2) / 8;
            const int bitOffset = 6 - ((x * 2) % 8);
            rowBuffer[byteIndex] |= (twoBit << bitOffset);
          }
          if (atkinsonDitherer)
            atkinsonDitherer->nextRow();
          else if (fsDitherer)
            fsDitherer->nextRow();
        }
        bmpOut.write(rowBuffer, bytesPerRow);
      } else {
        // Fixed-point area averaging for exact fit scaling
        // For each output pixel X, accumulate source pixels that map to it
        // srcX range for outX: [outX * scaleX_fp >> 16, (outX+1) * scaleX_fp >> 16)
        const uint8_t* srcRow = mcuRowBuffer + bufferY * imageInfo.m_width;

        for (int outX = 0; outX < outWidth; outX++) {
          // Calculate source X range for this output pixel
          const int srcXStart = (static_cast<uint32_t>(outX) * scaleX_fp) >> 16;
          const int srcXEnd = (static_cast<uint32_t>(outX + 1) * scaleX_fp) >> 16;

          // Accumulate all source pixels in this range
          int sum = 0;
          int count = 0;
          for (int srcX = srcXStart; srcX < srcXEnd && srcX < imageInfo.m_width; srcX++) {
            sum += srcRow[srcX];
            count++;
          }

          // Handle edge case: if no pixels in range, use nearest
          if (count == 0 && srcXStart < imageInfo.m_width) {
            sum = srcRow[srcXStart];
            count = 1;
          }

          rowAccum[outX] += sum;
          rowCount[outX] += count;
        }

        // Check if we've crossed into the next output row(s)
        // Current source Y in fixed point: y << 16
        const uint32_t srcY_fp = static_cast<uint32_t>(y + 1) << 16;

        // Output all rows whose boundaries we've crossed (handles both up and downscaling)
        // For upscaling, one source row may produce multiple output rows
        while (srcY_fp >= nextOutY_srcStart && currentOutY < outHeight) {
          memset(rowBuffer, 0, bytesPerRow);

          if (USE_8BIT_OUTPUT && !oneBit) {
            for (int x = 0; x < outWidth; x++) {
              const uint8_t gray = (rowCount[x] > 0) ? (rowAccum[x] / rowCount[x]) : 0;
              rowBuffer[x] = adjustPixel(gray);
            }
          } else if (oneBit) {
            // 1-bit output with Atkinson dithering for better quality
            for (int x = 0; x < outWidth; x++) {
              const uint8_t gray = (rowCount[x] > 0) ? (rowAccum[x] / rowCount[x]) : 0;
              const uint8_t bit = atkinson1BitDitherer ? atkinson1BitDitherer->processPixel(gray, x)
                                                       : quantize1bit(gray, x, currentOutY);
              // Pack 1-bit value: MSB first, 8 pixels per byte
              const int byteIndex = x / 8;
              const int bitOffset = 7 - (x % 8);
              rowBuffer[byteIndex] |= (bit << bitOffset);
            }
            if (atkinson1BitDitherer) atkinson1BitDitherer->nextRow();
          } else {
            // 2-bit output
            for (int x = 0; x < outWidth; x++) {
              const uint8_t gray = adjustPixel((rowCount[x] > 0) ? (rowAccum[x] / rowCount[x]) : 0);
              uint8_t twoBit;
              if (atkinsonDitherer) {
                twoBit = atkinsonDitherer->processPixel(gray, x);
              } else if (fsDitherer) {
                twoBit = fsDitherer->processPixel(gray, x);
              } else {
                twoBit = quantize(gray, x, currentOutY);
              }
              const int byteIndex = (x * 2) / 8;
              const int bitOffset = 6 - ((x * 2) % 8);
              rowBuffer[byteIndex] |= (twoBit << bitOffset);
            }
            if (atkinsonDitherer)
              atkinsonDitherer->nextRow();
            else if (fsDitherer)
              fsDitherer->nextRow();
          }

          bmpOut.write(rowBuffer, bytesPerRow);
          currentOutY++;

          // Update boundary for next output row
          nextOutY_srcStart = static_cast<uint32_t>(currentOutY + 1) * scaleY_fp;

          // For upscaling: don't reset accumulators if next output row uses same source data
          // Only reset when we'll move to a new source row
          if (srcY_fp >= nextOutY_srcStart) {
            // More output rows to emit from same source - keep accumulator data
            continue;
          }
          // Moving to next source row - reset accumulators
          memset(rowAccum, 0, outWidth * sizeof(uint32_t));
          memset(rowCount, 0, outWidth * sizeof(uint32_t));
        }
      }
    }
  }

  LOG_DBG("JPG", "Successfully converted JPEG to BMP");
  return true;
}

// Core function: Convert JPEG file to 2-bit BMP (uses default target size)
bool JpegToBmpConverter::jpegFileToBmpStream(FsFile& jpegFile, Print& bmpOut, bool crop) {
  return jpegFileToBmpStreamInternal(jpegFile, bmpOut, TARGET_MAX_WIDTH, TARGET_MAX_HEIGHT, false, crop);
}

// Convert with custom target size (for thumbnails, 2-bit)
bool JpegToBmpConverter::jpegFileToBmpStreamWithSize(FsFile& jpegFile, Print& bmpOut, int targetMaxWidth,
                                                     int targetMaxHeight) {
  return jpegFileToBmpStreamInternal(jpegFile, bmpOut, targetMaxWidth, targetMaxHeight, false);
}

// Convert to 1-bit BMP (black and white only, no grays) for fast home screen rendering
bool JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(FsFile& jpegFile, Print& bmpOut, int targetMaxWidth,
                                                         int targetMaxHeight) {
  return jpegFileToBmpStreamInternal(jpegFile, bmpOut, targetMaxWidth, targetMaxHeight, true, true);
}

#if CROSSPOINT_PAPERS3
#include <JPEGDEC.h>
#include <esp_heap_caps.h>

namespace {
struct ThumbDecodeCtx {
  uint8_t* grayBuf;
  int bufWidth;
  int bufHeight;
};

int thumbDrawCallback(JPEGDRAW* pDraw) {
  auto* ctx = static_cast<ThumbDecodeCtx*>(pDraw->pUser);
  for (int y = 0; y < pDraw->iHeight; y++) {
    const int destY = pDraw->y + y;
    if (destY >= ctx->bufHeight) break;
    const int rowOffset = destY * ctx->bufWidth;
    const int srcRowOffset = y * pDraw->iWidth;
    for (int x = 0; x < pDraw->iWidth; x++) {
      const int destX = pDraw->x + x;
      if (destX >= ctx->bufWidth) break;
      ctx->grayBuf[rowOffset + destX] = reinterpret_cast<uint8_t*>(pDraw->pPixels)[srcRowOffset + x];
    }
  }
  return 1;
}
}  // namespace

bool JpegToBmpConverter::jpegMemTo1BitBmp(const uint8_t* jpegData, size_t jpegSize, Print& bmpOut, int targetWidth,
                                          int targetHeight) {
  LOG_DBG("JPG", "JPEGDEC fast path: target %dx%d", targetWidth, targetHeight);

  // Heap-allocate JPEGDEC in PSRAM — the object is ~16KB (file buffer, Huffman
  // tables, MCU buffers) which overflows the 8KB render task stack.
  auto* jpegMem = static_cast<JPEGDEC*>(heap_caps_malloc(sizeof(JPEGDEC), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!jpegMem) {
    LOG_ERR("JPG", "Failed to allocate JPEGDEC (%d bytes)", sizeof(JPEGDEC));
    return false;
  }
  auto* jpeg = new (jpegMem) JPEGDEC();

  if (!jpeg->openRAM(const_cast<uint8_t*>(jpegData), jpegSize, thumbDrawCallback)) {
    LOG_ERR("JPG", "JPEGDEC: failed to open from RAM");
    jpeg->~JPEGDEC();
    heap_caps_free(jpegMem);
    return false;
  }

  const int origW = jpeg->getWidth();
  const int origH = jpeg->getHeight();

  // Choose the smallest JPEGDEC scale that still produces an image >= target size
  int scale = 0;
  if (origW >= targetWidth * 8 && origH >= targetHeight * 8)
    scale = JPEG_SCALE_EIGHTH;
  else if (origW >= targetWidth * 4 && origH >= targetHeight * 4)
    scale = JPEG_SCALE_QUARTER;
  else if (origW >= targetWidth * 2 && origH >= targetHeight * 2)
    scale = JPEG_SCALE_HALF;

  int scaledW = origW;
  int scaledH = origH;
  if (scale == JPEG_SCALE_EIGHTH) {
    scaledW = (origW + 7) / 8;
    scaledH = (origH + 7) / 8;
  } else if (scale == JPEG_SCALE_QUARTER) {
    scaledW = (origW + 3) / 4;
    scaledH = (origH + 3) / 4;
  } else if (scale == JPEG_SCALE_HALF) {
    scaledW = (origW + 1) / 2;
    scaledH = (origH + 1) / 2;
  }

  LOG_DBG("JPG", "JPEGDEC: %dx%d scale=1/%d -> %dx%d", origW, origH,
          scale == JPEG_SCALE_EIGHTH    ? 8
          : scale == JPEG_SCALE_QUARTER ? 4
          : scale == JPEG_SCALE_HALF    ? 2
                                        : 1,
          scaledW, scaledH);

  // Allocate grayscale decode buffer in PSRAM
  auto* grayBuf = static_cast<uint8_t*>(heap_caps_malloc(scaledW * scaledH, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!grayBuf) {
    LOG_ERR("JPG", "Failed to allocate grayscale buffer (%d bytes)", scaledW * scaledH);
    jpeg->close();
    jpeg->~JPEGDEC();
    heap_caps_free(jpegMem);
    return false;
  }
  memset(grayBuf, 0xFF, scaledW * scaledH);

  ThumbDecodeCtx ctx = {grayBuf, scaledW, scaledH};
  jpeg->setUserPointer(&ctx);
  jpeg->setPixelType(EIGHT_BIT_GRAYSCALE);

  const unsigned long t0 = millis();
  const int rc = jpeg->decode(0, 0, scale);
  LOG_DBG("JPG", "JPEGDEC decode took %lu ms", millis() - t0);

  // Done with decoder — free before BMP writing
  jpeg->~JPEGDEC();
  heap_caps_free(jpegMem);

  if (!rc) {
    LOG_ERR("JPG", "JPEGDEC: decode failed");
    free(grayBuf);
    return false;
  }

  // Calculate output dimensions maintaining aspect ratio (crop to fill)
  const float scaleToFitW = static_cast<float>(targetWidth) / scaledW;
  const float scaleToFitH = static_cast<float>(targetHeight) / scaledH;
  const float fitScale = (scaleToFitW > scaleToFitH) ? scaleToFitW : scaleToFitH;
  int outW = static_cast<int>(scaledW * fitScale);
  int outH = static_cast<int>(scaledH * fitScale);
  if (outW < 1) outW = 1;
  if (outH < 1) outH = 1;

  // Write 1-bit BMP header
  writeBmpHeader1bit(bmpOut, outW, outH);
  const int bytesPerRow = (outW + 31) / 32 * 4;

  auto* rowBuf = static_cast<uint8_t*>(malloc(bytesPerRow));
  if (!rowBuf) {
    LOG_ERR("JPG", "Failed to allocate row buffer");
    free(grayBuf);
    return false;
  }

  Atkinson1BitDitherer ditherer(outW);

  // Fixed-point scale factors (output → source mapping)
  const uint32_t scaleX_fp = (static_cast<uint32_t>(scaledW) << 16) / outW;
  const uint32_t scaleY_fp = (static_cast<uint32_t>(scaledH) << 16) / outH;

  for (int y = 0; y < outH; y++) {
    memset(rowBuf, 0, bytesPerRow);
    int srcY = (static_cast<uint32_t>(y) * scaleY_fp) >> 16;
    if (srcY >= scaledH) srcY = scaledH - 1;

    for (int x = 0; x < outW; x++) {
      int srcX = (static_cast<uint32_t>(x) * scaleX_fp) >> 16;
      if (srcX >= scaledW) srcX = scaledW - 1;

      const uint8_t gray = grayBuf[srcY * scaledW + srcX];
      const uint8_t bit = ditherer.processPixel(gray, x);
      const int byteIdx = x / 8;
      const int bitOff = 7 - (x % 8);
      rowBuf[byteIdx] |= (bit << bitOff);
    }
    ditherer.nextRow();
    bmpOut.write(rowBuf, bytesPerRow);
  }

  free(rowBuf);
  free(grayBuf);

  LOG_DBG("JPG", "JPEGDEC fast path: thumb BMP generated successfully");
  return true;
}
#endif
