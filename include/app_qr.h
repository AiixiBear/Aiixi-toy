// QR business card: shows a QR code for https://a1o.uk.
//
// Encoded with Nayuki's qrcodegen (MIT, vendored in include/qrcodegen.h
// and src/qrcodegen.c). The URL is short, so it fits in a version 2
// code (25x25 modules) at medium error correction. The code is always
// drawn black on a white block, regardless of the UI theme, because
// phone cameras expect dark modules on a light background.
//
// Layout: 3 px per module on the Plus2, 2 px on the original StickC
// (where the quiet zone is 3 modules instead of 4 so the code still
// fits the 80 px tall screen).

#ifndef APP_QR_H
#define APP_QR_H

#include "qrcodegen.h"

#define QR_URL "https://a1o.uk"
#define QR_MAX_VERSION 4

static uint8_t qrTempBuffer[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VERSION)];
static uint8_t qrCodeBuffer[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VERSION)];

void qr_screen() {
  bool ok = qrcodegen_encodeText(QR_URL, qrTempBuffer, qrCodeBuffer,
                                 qrcodegen_Ecc_MEDIUM, 1, QR_MAX_VERSION,
                                 qrcodegen_Mask_AUTO, true);
  bool needRepaint = true;

  while (!appExitRequested()) {
    if (needRepaint) {
      needRepaint = false;
      M5.Lcd.fillRect(0, toolbar_height, screen_x, screen_y - toolbar_height, bg_color);
      if (!ok) {
        M5.Lcd.setFont(&beta8pt7b);
        M5.Lcd.setTextColor(txt_color, bg_color);
        M5.Lcd.setCursor(10, (current_screen == STICKC) ? 45 : 77);
        M5.Lcd.print("QR FAILED");
        delay(10);
        continue;
      }

      const int scale = (current_screen == STICKC) ? 2 : 3;
      const int quiet = (current_screen == STICKC) ? 3 : 4;
      int size = qrcodegen_getSize(qrCodeBuffer);
      int block = (size + quiet * 2) * scale;
      int qrX = 8;
      int qrY = toolbar_height + (screen_y - toolbar_height - block) / 2;

      // White block, then black modules.
      M5.Lcd.fillRect(qrX, qrY, block, block, TFT_WHITE);
      for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
          if (qrcodegen_getModule(qrCodeBuffer, x, y)) {
            M5.Lcd.fillRect(qrX + (x + quiet) * scale, qrY + (y + quiet) * scale,
                            scale, scale, TFT_BLACK);
          }
        }
      }

      int textX = qrX + block + 12;
      const bool big = (current_screen != STICKC);
      M5.Lcd.setTextColor(txt_color, bg_color);
      M5.Lcd.setFont(big ? &beta8pt7b : &beta5pt7b);
      M5.Lcd.setCursor(textX, qrY + block / 4);
      M5.Lcd.print("Aiixi Bear");
      M5.Lcd.setFont(big ? &beta10pt7b : &beta8pt7b);
      M5.Lcd.setCursor(textX, qrY + block / 2);
      M5.Lcd.print("a1o.uk");
      M5.Lcd.setFont(&beta5pt7b);
      M5.Lcd.setCursor(textX, qrY + block * 3 / 4);
      M5.Lcd.print("SCAN ME");
    }
    delay(10);
  }
}

#endif
