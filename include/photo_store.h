// Photo frame storage: JPEGs uploaded through the WebUI, stored in the
// 128 KB SPIFFS partition (min_spiffs.csv). The partition is shared
// with nothing else, but SPIFFS needs slack, so the budget below stays
// well under the full size.
//
// Limits (v1, documented in the README and the WebUI):
//   at most 6 photos, each at most 24 KB, 110 KB total.
//   Baseline JPEG only: the TJpgDec decoder in M5GFX cannot draw
//   progressive JPEGs, so those are detected here and skipped.

#ifndef PHOTO_STORE_H
#define PHOTO_STORE_H

#include <SPIFFS.h>

#define PHOTO_DIR "/photos"
#define PHOTO_MAX_COUNT 6
#define PHOTO_MAX_BYTES 24576   // 24 KB per photo
#define PHOTO_TOTAL_BUDGET 110000

static bool photoFsReady = false;

bool photoFsBegin() {
  if (photoFsReady) {
    return true;
  }
  photoFsReady = SPIFFS.begin(true);
  return photoFsReady;
}

// List photo file names (without the directory prefix) into out[].
// Returns the number of photos found, at most maxCount.
int photoList(String* out, int maxCount) {
  if (!photoFsBegin()) {
    return 0;
  }
  File dir = SPIFFS.open(PHOTO_DIR);
  if (!dir) {
    return 0;
  }
  int n = 0;
  File f = dir.openNextFile();
  while (f && n < maxCount) {
    String name = String(f.name());
    if (name.endsWith(".jpg") || name.endsWith(".jpeg")) {
      out[n++] = name.substring(String(PHOTO_DIR).length() + 1);
    }
    f = dir.openNextFile();
  }
  return n;
}

size_t photoTotalBytes() {
  String names[PHOTO_MAX_COUNT];
  int n = photoList(names, PHOTO_MAX_COUNT);
  size_t total = 0;
  for (int i = 0; i < n; i++) {
    File f = SPIFFS.open(String(PHOTO_DIR) + "/" + names[i]);
    if (f) {
      total += f.size();
      f.close();
    }
  }
  return total;
}

// First free slot number (1..PHOTO_MAX_COUNT), or -1 when full.
// Uploaded photos are stored as <slot>.jpg, so the browser's original
// file name is never used as a path.
int photoNextSlot() {
  String names[PHOTO_MAX_COUNT];
  int n = photoList(names, PHOTO_MAX_COUNT);
  for (int slot = 1; slot <= PHOTO_MAX_COUNT; slot++) {
    bool used = false;
    String want = String(slot) + ".jpg";
    for (int i = 0; i < n; i++) {
      if (names[i] == want) {
        used = true;
        break;
      }
    }
    if (!used) {
      return slot;
    }
  }
  return -1;
}

// Parse JPEG dimensions from the SOF marker, walking the segments with
// seeks so large EXIF blocks before the SOF do not matter. Returns
// false for non-JPEGs, for progressive JPEGs (SOF2, which the decoder
// cannot draw), and for truncated files.
bool photoDimensions(const String& path, int& w, int& h) {
  File f = SPIFFS.open(path);
  if (!f) {
    return false;
  }
  uint8_t soib[2];
  if (f.read(soib, 2) != 2 || soib[0] != 0xFF || soib[1] != 0xD8) {
    f.close();
    return false;
  }
  while (true) {
    int c;
    do {
      c = f.read();
      if (c < 0) {
        f.close();
        return false;
      }
    } while (c != 0xFF);
    int marker;
    do {
      marker = f.read();
      if (marker < 0) {
        f.close();
        return false;
      }
    } while (marker == 0xFF);
    if (marker == 0x00 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
      continue;
    }
    if (marker == 0xDA) {
      // Start of scan: no SOF marker before the image data.
      f.close();
      return false;
    }
    if (marker == 0xC0 || marker == 0xC2) {
      if (marker == 0xC2) {
        // Progressive JPEG: TJpgDec cannot decode it.
        f.close();
        return false;
      }
      uint8_t hdr[7];
      if (f.read(hdr, 7) != 7) {
        f.close();
        return false;
      }
      h = (hdr[3] << 8) | hdr[4];
      w = (hdr[5] << 8) | hdr[6];
      f.close();
      return w > 0 && h > 0;
    }
    uint8_t lenb[2];
    if (f.read(lenb, 2) != 2) {
      f.close();
      return false;
    }
    uint16_t len = (lenb[0] << 8) | lenb[1];
    if (len < 2 || !f.seek(f.position() + len - 2)) {
      f.close();
      return false;
    }
  }
}

#endif
