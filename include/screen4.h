// Time drift correction screen - adjust RTC by minutes and seconds

#define DRIFT_DOUBLE_TAP_MS  600
#define DRIFT_LONG_PRESS_MS  500

void Time_drift_screen() {
  bool firstloadScreen = true;
  int8_t adjust_minutes = 0;
  int8_t adjust_seconds = 0;
  uint8_t selected_field = 0;  // 0 = minutes, 1 = seconds
  int8_t last_adj_m = 0;
  int8_t last_adj_s = 0;
  uint8_t last_sel = 255;

  bool btn_was_down = false;
  uint32_t last_release_ms = 0;
  bool long_press_handled = false;
  bool pending_redraw = false;

  while (!appExitRequested()) {

    M5.update();

    if (pending_redraw) {
      pending_redraw = false;
      last_adj_m = adjust_minutes;
      last_adj_s = adjust_seconds;
      last_sel = selected_field;
      M5.Lcd.fillRect(0, toolbar_height, screen_x, screen_y - toolbar_height, bg_color);
      if (current_screen == STICKC) {
        M5.Lcd.setFont(&beta8pt7b);
        M5.Lcd.setCursor(5, 20);
      } else {
        M5.Lcd.setFont(&beta10pt7b);
        M5.Lcd.setCursor(5, 30);
      }
      M5.Lcd.setTextColor(txt_color, bg_color);
      M5.Lcd.print("Fix Time Drift");
      localTime = M5.Rtc.getDateTime();
      getCurrentLocalTime(&localTime, timezone_h, timezone_m);
      String currentTime = getString_2digit(localTime.time.hours) + ":" +
                           getString_2digit(localTime.time.minutes) + ":" +
                           getString_2digit(localTime.time.seconds);
      if (current_screen == STICKC) {
        M5.Lcd.setFont(&beta8pt7b);
        M5.Lcd.setCursor(5, 34);
      } else {
        M5.Lcd.setFont(&beta10pt7b);
        M5.Lcd.setCursor(5, 55);
      }
      M5.Lcd.print(currentTime);
      if (current_screen == STICKC) {
        M5.Lcd.setFont(&beta5pt7b);
        M5.Lcd.setCursor(5, 50);
      } else {
        M5.Lcd.setFont(&beta8pt7b);
        M5.Lcd.setCursor(5, 85);
      }
      M5.Lcd.setTextColor(selected_field == 0 ? TFT_YELLOW : txt_color, bg_color);
      M5.Lcd.printf("Minutes: %+d", (int)adjust_minutes);
      if (current_screen == STICKC) {
        M5.Lcd.setCursor(5, 60);
      } else {
        M5.Lcd.setCursor(5, 105);
      }
      M5.Lcd.setTextColor(selected_field == 1 ? TFT_YELLOW : txt_color, bg_color);
      M5.Lcd.printf("Seconds: %+d", (int)adjust_seconds);
      // Shortcuts: BtnA tap +1 (minutes, or +10 seconds), BtnA double-tap
      // switches field, BtnA hold -1 (-10 seconds), BtnB next screen.
      M5.Lcd.setFont(&beta5pt7b);
      M5.Lcd.setTextColor(txt_color, bg_color);
      if (current_screen == STICKC) {
        M5.Lcd.setCursor(5, 70);
        M5.Lcd.print("A:+1 dbl:fld hold:-1 B:nxt");
      } else {
        M5.Lcd.setCursor(5, 120);
        M5.Lcd.print("A:tap +1  dbl:field");
        M5.Lcd.setCursor(5, 128);
        M5.Lcd.print("A:hold -1  B:next");
      }
    }

    uint32_t now_ms = millis();
    bool btn_down = M5.BtnA.isPressed();

    // Long press: decrement (fire once per hold)
    if (btn_down && M5.BtnA.pressedFor(DRIFT_LONG_PRESS_MS) && !long_press_handled) {
      long_press_handled = true;
      btn_was_down = true;

      time_t t;
      if (selected_field == 0) {
        adjust_minutes--;
        adjust_minutes = (adjust_minutes < -59) ? -59 : adjust_minutes;
        tm rtc_tm = M5.Rtc.getDateTime().get_tm();
        t = mktime(&rtc_tm);
        t -= 60;
      } else {
        adjust_seconds -= 10;
        adjust_seconds = (adjust_seconds < -59) ? -59 : adjust_seconds;
        tm rtc_tm = M5.Rtc.getDateTime().get_tm();
        t = mktime(&rtc_tm);
        t -= 10;
      }
      struct tm* adj = localtime(&t);
      M5.Rtc.setDateTime(adj);
      setTime(adj->tm_hour, adj->tm_min, adj->tm_sec, adj->tm_mday, adj->tm_mon + 1, adj->tm_year + 1900);
      NVS.setInt("last_time_synced", (int)t);
      previousMillis = millis();
    }
    // Short press: detect release after brief hold (no long press)
    else if (btn_was_down && !btn_down && !long_press_handled) {
      bool is_double = (last_release_ms != 0 && (now_ms - last_release_ms) < DRIFT_DOUBLE_TAP_MS);

      if (is_double) {
        selected_field = 1 - selected_field;
        last_release_ms = 0;
// M5.Speaker.tone(3000, 80);
      } else {
        time_t t;
        tm rtc_tm = M5.Rtc.getDateTime().get_tm();
        t = mktime(&rtc_tm);
        if (selected_field == 0) {
          adjust_minutes++;
          adjust_minutes = (adjust_minutes > 59) ? 59 : adjust_minutes;
          t += 60;
        } else {
          adjust_seconds += 10;
          adjust_seconds = (adjust_seconds > 59) ? 59 : adjust_seconds;
          t += 10;
        }
        struct tm* adj = localtime(&t);
        M5.Rtc.setDateTime(adj);
        setTime(adj->tm_hour, adj->tm_min, adj->tm_sec, adj->tm_mday, adj->tm_mon + 1, adj->tm_year + 1900);
        NVS.setInt("last_time_synced", (int)t);
// M5.Speaker.tone(2000, 50);
        last_release_ms = now_ms;
      }
      previousMillis = millis();
    }

    if (!btn_down) {
      long_press_handled = false;
    }
    btn_was_down = btn_down;

    bool need_redraw = firstloadScreen || adjust_minutes != last_adj_m ||
                       adjust_seconds != last_adj_s || selected_field != last_sel;
    if (need_redraw) {
      if (firstloadScreen) {
        firstloadScreen = false;
        last_adj_m = adjust_minutes;
        last_adj_s = adjust_seconds;
        last_sel = selected_field;
        M5.Lcd.fillRect(0, toolbar_height, screen_x, screen_y - toolbar_height, bg_color);
        if (current_screen == STICKC) {
          M5.Lcd.setFont(&beta8pt7b);
          M5.Lcd.setCursor(5, 20);
        } else {
          M5.Lcd.setFont(&beta10pt7b);
          M5.Lcd.setCursor(5, 30);
        }
        M5.Lcd.setTextColor(txt_color, bg_color);
        M5.Lcd.print("Fix Time Drift");
        localTime = M5.Rtc.getDateTime();
        getCurrentLocalTime(&localTime, timezone_h, timezone_m);
        String currentTime = getString_2digit(localTime.time.hours) + ":" +
                             getString_2digit(localTime.time.minutes) + ":" +
                             getString_2digit(localTime.time.seconds);
        if (current_screen == STICKC) {
          M5.Lcd.setFont(&beta8pt7b);
          M5.Lcd.setCursor(5, 34);
        } else {
          M5.Lcd.setFont(&beta10pt7b);
          M5.Lcd.setCursor(5, 55);
        }
        M5.Lcd.print(currentTime);
        if (current_screen == STICKC) {
          M5.Lcd.setFont(&beta5pt7b);
          M5.Lcd.setCursor(5, 50);
        } else {
          M5.Lcd.setFont(&beta8pt7b);
          M5.Lcd.setCursor(5, 85);
        }
        M5.Lcd.setTextColor(selected_field == 0 ? TFT_YELLOW : txt_color, bg_color);
        M5.Lcd.printf("Minutes: %+d", (int)adjust_minutes);
        if (current_screen == STICKC) {
          M5.Lcd.setCursor(5, 60);
        } else {
          M5.Lcd.setCursor(5, 105);
        }
        M5.Lcd.setTextColor(selected_field == 1 ? TFT_YELLOW : txt_color, bg_color);
        M5.Lcd.printf("Seconds: %+d", (int)adjust_seconds);
        // Shortcuts: BtnA tap +1 (minutes, or +10 seconds), BtnA double-tap
        // switches field, BtnA hold -1 (-10 seconds), BtnB next screen.
        M5.Lcd.setFont(&beta5pt7b);
        M5.Lcd.setTextColor(txt_color, bg_color);
        if (current_screen == STICKC) {
          M5.Lcd.setCursor(5, 70);
          M5.Lcd.print("A:+1 dbl:fld hold:-1 B:nxt");
        } else {
          M5.Lcd.setCursor(5, 120);
          M5.Lcd.print("A:tap +1  dbl:field");
          M5.Lcd.setCursor(5, 128);
          M5.Lcd.print("A:hold -1  B:next");
        }
      } else {
        pending_redraw = true;
      }
    }

    delay(10);
  }
}
