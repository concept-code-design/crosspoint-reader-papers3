#pragma once

#include <HalStorage.h>

class Print;
class ZipFile;

class JpegToBmpConverter {
  static unsigned char jpegReadCallback(unsigned char* pBuf, unsigned char buf_size,
                                        unsigned char* pBytes_actually_read, void* pCallback_data);
  static bool jpegFileToBmpStreamInternal(FsFile& jpegFile, Print& bmpOut, int targetWidth, int targetHeight,
                                          bool oneBit, bool crop = true);

 public:
  static bool jpegFileToBmpStream(FsFile& jpegFile, Print& bmpOut, bool crop = true);
  // Convert with custom target size (for thumbnails)
  static bool jpegFileToBmpStreamWithSize(FsFile& jpegFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight);
  // Convert to 1-bit BMP (black and white only, no grays) for fast home screen rendering
  static bool jpegFileTo1BitBmpStreamWithSize(FsFile& jpegFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight);

#if CROSSPOINT_PAPERS3
  // Fast path: decode JPEG from RAM using JPEGDEC with hardware downscaling.
  // Requires PSRAM for the grayscale decode buffer. ~10-50x faster than picojpeg.
  static bool jpegMemTo1BitBmp(const uint8_t* jpegData, size_t jpegSize, Print& bmpOut, int targetWidth,
                               int targetHeight);
#endif
};
