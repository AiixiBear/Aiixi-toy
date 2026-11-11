// Decibel meter using the SPM1423 microphone.
//
// Samples the microphone at 16 kHz in 32 ms windows, computes the RMS
// level and converts it to an estimated sound pressure level.
//
// Calibration: this is an ESTIMATE, not a calibrated measurement. The
// SPM1423 datasheet sensitivity is -26 dBFS at 94 dB SPL (1 kHz), so
// SPL is estimated as dBFS + 120. The PDM-to-PCM path and the mounting
// inside the case add their own gain and resonances, so expect the
// absolute reading to be off by several dB; relative changes (quieter
// vs louder) are meaningful. The screen and the README say so.
//
// BtnA short press resets the peak hold.

#ifndef APP_DB_H
#define APP_DB_H

#include <math.h>

// Datasheet sensitivity: -26 dBFS at 94 dB SPL.
#define DB_SPL_OFFSET 120.0f
#define DB_FLOOR 30.0f
#define DB_CEIL 120.0f

void db_screen() {
  static int16_t samples[512];
  float smooth = 0;
  float peak = 0;
  bool haveReading = false;
  bool micOk = true;
  bool needRepaint = true;
  unsigned long lastDraw = 0;

  while (!appExitRequested()) {
    if (M5.Mic.record(samples, 512, 16000)) {
      double sumSq = 0;
      for (int i = 0; i < 512; i++) {
        double s = samples[i];
        sumSq += s * s;
      }
      double rms = sqrt(sumSq / 512.0);
      float spl = DB_FLOOR;
      if (rms > 0) {
        spl = 20.0f * log10f((float)(rms / 32768.0)) + DB_SPL_OFFSET;
      }
      spl = constrain(spl, DB_FLOOR, DB_CEIL);
      smooth = haveReading ? smooth * 0.6f + spl * 0.4f : spl;
      if (spl > peak) {
        peak = spl;
      }
      haveReading = true;
      micOk = true;
    } else {
      micOk = false;
    }

    if (M5.BtnA.wasClicked()) {
      previousMillis = millis();
      peak = smooth;
      needRepaint = true;
    }

    if (millis() - lastDraw >= 100 && haveReading) {
      lastDraw = millis();
      needRepaint = true;
    }

    if (needRepaint) {
      needRepaint = false;
      M5.Lcd.fillRect(0, toolbar_height, screen_x, screen_y - toolbar_height, bg_color);
      M5.Lcd.setTextColor(txt_color, bg_color);

      if (!micOk) {
        M5.Lcd.setFont(&beta8pt7b);
        M5.Lcd.setCursor(10, (current_screen == STICKC) ? 45 : 77);
        M5.Lcd.print("NO MIC");
        delay(10);
        continue;
      }

      const bool big = (current_screen != STICKC);
      const int numY = big ? toolbar_height + 42 : toolbar_height + 30;
      const int barY = big ? toolbar_height + 62 : toolbar_height + 44;
      const int labelY = big ? toolbar_height + 92 : toolbar_height + 62;

      M5.Lcd.setFont(big ? &beta15pt7b : &beta10pt7b);
      String num = String(smooth, 1);
      M5.Lcd.setCursor(10, numY);
      M5.Lcd.print(num);
      int numW = M5.Lcd.textWidth(num);
      M5.Lcd.setFont(&beta8pt7b);
      M5.Lcd.setCursor(14 + numW, numY + (big ? 2 : 0));
      M5.Lcd.print("dB");

      // Level bar, 30..110 dB across the screen width.
      const int barX = 10;
      const int barW = screen_x - 20;
      const int barH = big ? 12 : 8;
      float frac = (smooth - 30.0f) / 80.0f;
      frac = constrain(frac, 0.0f, 1.0f);
      M5.Lcd.drawRect(barX, barY, barW, barH, txt_color);
      M5.Lcd.fillRect(barX + 1, barY + 1, (int)((barW - 2) * frac), barH - 2, txt_color);

      M5.Lcd.setFont(&beta5pt7b);
      M5.Lcd.setCursor(10, labelY);
      M5.Lcd.print("PEAK " + String(peak, 1) + " dB");
      M5.Lcd.setCursor(10, labelY + 12);
      M5.Lcd.print("ESTIMATE - NOT CALIBRATED");
    }

    delay(10);
  }
}

#endif
