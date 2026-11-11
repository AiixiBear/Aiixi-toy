// Photo frame: slideshow of the JPEGs uploaded through the WebUI.
//
// Photos are scaled to fit the screen (never upscaled) and centered.
// BtnA short press shows the next photo; the frame also advances by
// itself every 10 seconds. Progressive JPEGs cannot be decoded by the
// TJpgDec decoder in M5GFX, so photoDimensions() filters them out of
// the slideshow; a photo that fails to decode at show time is skipped
// with a short notice instead of hanging the app.

#ifndef APP_PHOTO_H
#define APP_PHOTO_H

#include "photo_store.h"

#define PHOTO_SLIDE_MS 10000

void photoShowOne(const String& name, int idx, int count) {
  String path = String(PHOTO_DIR) + "/" + name;
  int w = 0, h = 0;
  photoDimensions(path, w, h);
  float scale = 1.0f;
  if (w > 0 && h > 0) {
    scale = fminf((float)screen_x / (float)w,
                  (float)(screen_y - toolbar_height) / (float)h);
    if (scale > 1.0f) {
      scale = 1.0f;
    }
  }
  int dw = (int)(w * scale);
  int dh = (int)(h * scale);
  int x = (screen_x - dw) / 2;
  int y = toolbar_height + (screen_y - toolbar_height - dh) / 2;

  M5.Lcd.fillRect(0, toolbar_height, screen_x, screen_y - toolbar_height, bg_color);
  // Both scale arguments must be passed: scale_y defaults to 0, which
  // would squash the image flat.
  bool ok = M5.Lcd.drawJpgFile(SPIFFS, path.c_str(), x, y, screen_x,
                               screen_y - toolbar_height, 0, 0, scale, scale);
  if (!ok) {
    M5.Lcd.setFont(&beta8pt7b);
    M5.Lcd.setTextColor(txt_color, bg_color);
    M5.Lcd.setCursor(10, (current_screen == STICKC) ? 45 : 77);
    M5.Lcd.print("BAD FILE: " + name);
    return;
  }
  // Small caption strip at the bottom: index and file name.
  String cap = String(idx + 1) + "/" + String(count) + " " + name;
  M5.Lcd.setFont(&beta5pt7b);
  int capW = M5.Lcd.textWidth(cap);
  M5.Lcd.fillRect(0, screen_y - 11, capW + 12, 11, bg_color);
  M5.Lcd.setTextColor(txt_color, bg_color);
  M5.Lcd.setCursor(6, screen_y - 3);
  M5.Lcd.print(cap);
}

void photo_screen() {
  String names[PHOTO_MAX_COUNT];
  int total = photoList(names, PHOTO_MAX_COUNT);
  String good[PHOTO_MAX_COUNT];
  int count = 0;
  for (int i = 0; i < total; i++) {
    int w = 0, h = 0;
    if (photoDimensions(String(PHOTO_DIR) + "/" + names[i], w, h)) {
      good[count++] = names[i];
    }
  }

  int idx = 0;
  unsigned long lastShow = 0;
  bool needRepaint = true;

  while (!appExitRequested()) {
    if (count == 0) {
      if (needRepaint) {
        needRepaint = false;
        M5.Lcd.fillRect(0, toolbar_height, screen_x, screen_y - toolbar_height, bg_color);
        M5.Lcd.setFont(&beta8pt7b);
        M5.Lcd.setTextColor(txt_color, bg_color);
        M5.Lcd.setCursor(10, (current_screen == STICKC) ? 45 : 77);
        M5.Lcd.print(total > 0 ? "PHOTOS UNSUPPORTED" : "NO PHOTOS");
        M5.Lcd.setFont(&beta5pt7b);
        M5.Lcd.setCursor(10, (current_screen == STICKC) ? 65 : 97);
        M5.Lcd.print(total > 0 ? "BASELINE JPEG ONLY" : "UPLOAD IN WEBUI");
      }
      delay(10);
      continue;
    }

    if (M5.BtnA.wasClicked()) {
      previousMillis = millis();
      idx = (idx + 1) % count;
      needRepaint = true;
    }

    if (millis() - lastShow >= PHOTO_SLIDE_MS) {
      if (!needRepaint) {
        idx = (idx + 1) % count;
        needRepaint = true;
      }
    }

    if (needRepaint) {
      needRepaint = false;
      lastShow = millis();
      photoShowOne(good[idx], idx, count);
    }

    delay(10);
  }
}

#endif
