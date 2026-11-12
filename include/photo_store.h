// Photo frame storage: JPEGs uploaded through the WebUI, stored in the
// 4.125MB SPIFFS partition (partitions-8mb-photos.csv, Plus 2 only).
// SPIFFS needs slack, so the budget below stays well under the full size.
//
// Limits (documented in the README and the WebUI):
//   at most 20 photos, each at most 500KB, 3MB total.
//   500KB lets a 1080p JPEG through without resizing; the device
//   downscales it to the 240x135 screen. Decoding very large
//   (multi-megapixel) images is slow, which is why the per-file cap
//   is 500KB and not unlimited.
//   Baseline JPEG only: the TJpgDec decoder in M5GFX cannot draw
//   progressive JPEGs, so those are detected here and skipped.

#ifndef PHOTO_STORE_H
#define PHOTO_STORE_H

#include <SPIFFS.h>

#define PHOTO_DIR "/photos"
#define PHOTO_MAX_COUNT 20
#define PHOTO_MAX_BYTES 512000  // 500 KB per photo
#define PHOTO_TOTAL_BUDGET 3000000  // ~2.86MB total, SPIFFS slack kept

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

// Walk JPEG markers in a memory buffer. Returns 1 when a baseline
// SOF was found (w/h set), 0 when the data is not a decodable
// baseline JPEG, and -1 when the buffer ended before any SOF.
int photoJpegWalkMem(const uint8_t* buf, size_t len, int& w, int& h) {
  if (len < 2 || buf[0] != 0xFF || buf[1] != 0xD8) {
    return 0;
  }
  size_t pos = 2;
  while (true) {
    while (pos < len && buf[pos] != 0xFF) {
      pos++;
    }
    if (pos >= len) {
      return -1;
    }
    while (pos < len && buf[pos] == 0xFF) {
      pos++;
    }
    if (pos >= len) {
      return -1;
    }
    int marker = buf[pos++];
    if (marker == 0x00 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
      continue;
    }
    if (marker == 0xDA) {
      // Start of scan: no SOF marker before the image data.
      return 0;
    }
    if (marker == 0xC0 || marker == 0xC2) {
      if (marker == 0xC2) {
        // Progressive JPEG: TJpgDec cannot decode it.
        return 0;
      }
      if (pos + 7 > len) {
        return -1;
      }
      h = (buf[pos + 3] << 8) | buf[pos + 4];
      w = (buf[pos + 5] << 8) | buf[pos + 6];
      return (w > 0 && h > 0) ? 1 : 0;
    }
    if (pos + 2 > len) {
      return -1;
    }
    uint16_t segLen = (buf[pos] << 8) | buf[pos + 1];
    if (segLen < 2) {
      return 0;
    }
    pos += segLen;
    if (pos > len) {
      return -1;
    }
  }
}

// Parse JPEG dimensions from the SOF marker. Returns false for
// non-JPEGs, for progressive JPEGs (SOF2, which the decoder cannot
// draw), and for truncated files.
//
// The header (everything before the scan data) is buffered and
// walked in memory: the original implementation walked the file
// with single-byte reads and seeks, which on the real device
// reported even valid baseline files as unsupported while the same
// bytes parsed fine off-device. When the SOF lies beyond the
// buffered head (a header block larger than PHOTO_HEAD_BYTES), the
// walk falls back to streaming reads from the start of the file.
#define PHOTO_HEAD_BYTES 65536

bool photoDimensions(const String& path, int& w, int& h) {
  File f = SPIFFS.open(path);
  if (!f) {
    return false;
  }
  size_t fileSize = f.size();
  size_t headLen = fileSize < PHOTO_HEAD_BYTES ? fileSize : PHOTO_HEAD_BYTES;
  uint8_t* head = (uint8_t*)ps_malloc(headLen);
  if (head == nullptr) {
    head = (uint8_t*)malloc(headLen);
  }
  if (head != nullptr) {
    size_t got = f.read(head, headLen);
    int r = photoJpegWalkMem(head, got, w, h);
    free(head);
    if (r >= 0) {
      f.close();
      return r == 1;
    }
    f.seek(0);
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
