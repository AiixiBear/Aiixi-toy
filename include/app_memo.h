// Memo quick view: a scrollable list of the memo lines edited in the
// WebUI. BtnA short press moves to the next memo. Lines longer than
// the screen are truncated; edit them shorter in the WebUI if the end
// matters.

#ifndef APP_MEMO_H
#define APP_MEMO_H

#include "memo_cfg.h"

#define MEMO_MAX_LINES 30

void memo_screen() {
  const int rowH = (current_screen == STICKC) ? 31 : 38;
  const int visibleRows = (screen_y - toolbar_height) / rowH;

  String lines[MEMO_MAX_LINES];
  int count = 0;
  String all = memoGet();
  int start = 0;
  while (count < MEMO_MAX_LINES) {
    int nl = all.indexOf('\n', start);
    String line;
    if (nl < 0) {
      line = all.substring(start);
      if (line.length() > 0) {
        lines[count++] = line;
      }
      break;
    }
    line = all.substring(start, nl);
    if (line.length() > 0) {
      lines[count++] = line;
    }
    start = nl + 1;
  }

  int sel = 0;
  int winStart = 0;
  bool needRepaint = true;

  while (!appExitRequested()) {
    if (M5.BtnA.wasClicked()) {
      previousMillis = millis();
      if (count > 0) {
        sel = (sel + 1) % count;
        if (sel < winStart) {
          winStart = sel;
        }
        if (sel >= winStart + visibleRows) {
          winStart = sel - visibleRows + 1;
        }
      }
      needRepaint = true;
    }

    if (needRepaint) {
      needRepaint = false;
      M5.Lcd.fillRect(0, toolbar_height, screen_x, screen_y - toolbar_height, bg_color);
      M5.Lcd.setTextColor(txt_color, bg_color);
      if (count == 0) {
        M5.Lcd.setFont(&beta8pt7b);
        M5.Lcd.setCursor(10, (current_screen == STICKC) ? 45 : 77);
        M5.Lcd.print("NO MEMOS");
        M5.Lcd.setFont(&beta5pt7b);
        M5.Lcd.setCursor(10, (current_screen == STICKC) ? 65 : 97);
        M5.Lcd.print("ADD IN WEBUI");
      } else {
        M5.Lcd.setFont(&beta8pt7b);
        for (int r = 0; r < visibleRows; r++) {
          int idx = winStart + r;
          if (idx >= count) {
            break;
          }
          int rowTop = toolbar_height + r * rowH;
          bool selected = (idx == sel);
          if (selected) {
            M5.Lcd.fillRect(0, rowTop, screen_x, rowH, txt_color);
          }
          M5.Lcd.setTextColor(selected ? bg_color : txt_color, selected ? txt_color : bg_color);
          String line = lines[idx];
          while (line.length() > 0 && M5.Lcd.textWidth(line) > screen_x - 16) {
            line.remove(line.length() - 1);
          }
          M5.Lcd.setCursor(8, rowTop + rowH / 2 - 5);
          M5.Lcd.print(line);
        }
      }
    }

    delay(10);
  }
}

#endif
