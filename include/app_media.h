// BLE media remote: sends Consumer Control keys (play/pause, next,
// previous, volume, mute) to the paired host over the same BLE HID
// keyboard the TOTP typing uses.
//
// Same two-button convention as the OTP list screen: BtnA short press
// moves the selection, BtnA long press sends the selected key. Volume
// moves one step per long press. The host must be paired as a Bluetooth
// keyboard; the toolbar Bluetooth icon shows while it is connected.

#ifndef APP_MEDIA_H
#define APP_MEDIA_H

struct MediaAction {
  const char* label;
  uint8_t bit;
};

static const MediaAction mediaActions[] = {
  {"Play / Pause", BLE_HID_CC_PLAYPAUSE},
  {"Next Track",   BLE_HID_CC_NEXT},
  {"Prev Track",   BLE_HID_CC_PREV},
  {"Volume Up",    BLE_HID_CC_VOL_UP},
  {"Volume Down",  BLE_HID_CC_VOL_DOWN},
  {"Mute",         BLE_HID_CC_MUTE},
};
static const int mediaActionCount = sizeof(mediaActions) / sizeof(mediaActions[0]);

// Brief full-screen notice after sending, same pattern as the OTP
// typing notice in screen1.h.
void mediaNotice(const String& msg) {
  M5.Lcd.fillRect(0, toolbar_height, screen_x, screen_y - toolbar_height, bg_color);
  M5.Lcd.setFont(&beta8pt7b);
  M5.Lcd.setTextColor(txt_color, bg_color);
  M5.Lcd.setCursor(10, (current_screen == STICKC) ? 45 : 77);
  M5.Lcd.print(msg);
  delay(900);
}

void media_screen() {
  const int rowH = (current_screen == STICKC) ? 31 : 38;
  const int visibleRows = (screen_y - toolbar_height) / rowH;
  int sel = 0;
  int winStart = 0;
  bool needRepaint = true;

  while (!appExitRequested()) {
    if (M5.BtnA.wasClicked()) {
      previousMillis = millis();
      sel = (sel + 1) % mediaActionCount;
      if (sel < winStart) {
        winStart = sel;
      }
      if (sel >= winStart + visibleRows) {
        winStart = sel - visibleRows + 1;
      }
      needRepaint = true;
    }

    if (M5.BtnA.wasHold()) {
      previousMillis = millis();
      if (bleHidSendConsumer(mediaActions[sel].bit)) {
        mediaNotice("SENT");
      } else {
        mediaNotice("NO BT HOST");
      }
      needRepaint = true;
    }

    if (needRepaint) {
      needRepaint = false;
      M5.Lcd.fillRect(0, toolbar_height, screen_x, screen_y - toolbar_height, bg_color);
      for (int r = 0; r < visibleRows; r++) {
        int idx = winStart + r;
        if (idx >= mediaActionCount) {
          break;
        }
        int rowTop = toolbar_height + r * rowH;
        bool selected = (idx == sel);
        if (selected) {
          M5.Lcd.fillRect(0, rowTop, screen_x, rowH, txt_color);
        }
        M5.Lcd.setTextColor(selected ? bg_color : txt_color, selected ? txt_color : bg_color);
        M5.Lcd.setFont(&beta8pt7b);
        M5.Lcd.setCursor(8, rowTop + rowH / 2 - 5);
        M5.Lcd.print(mediaActions[idx].label);
        if (selected) {
          String pos = String(idx + 1) + "/" + String(mediaActionCount);
          M5.Lcd.setCursor(screen_x - 5 - M5.Lcd.textWidth(pos), rowTop + rowH / 2 - 5);
          M5.Lcd.print(pos);
        }
      }
    }

    delay(10);
  }
}

#endif
