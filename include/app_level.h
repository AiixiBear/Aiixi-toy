// Bubble level using the MPU6886 accelerometer.
//
// The bubble sits in the middle when the screen is horizontal and
// moves with the gravity vector as the device tilts. BtnA short press
// tares (zeroes) the bubble on the current surface, so an uneven table
// can be used as the reference. Angles are the tilt of the screen
// from horizontal in degrees.
//
// Axis mapping (bubble X from accel X, bubble Y from accel Y) follows
// the usual M5StickC orientation; if the bubble moves the wrong way on
// the real device, swap the sign of one axis here.

#ifndef APP_LEVEL_H
#define APP_LEVEL_H

#include <math.h>

void level_screen() {
  const int cx = screen_x / 4;
  const int cy = toolbar_height + (screen_y - toolbar_height) / 2;
  const int R = min((screen_y - toolbar_height) / 2 - 4, cx - 8);
  const int bubbleR = (current_screen == STICKC) ? 6 : 8;
  const int textX = cx + R + 12;

  float tareX = 0, tareY = 0;
  float smX = 0, smY = 0, smZ = 0;  // low-pass filtered accel
  bool haveReading = false;
  bool needRepaint = true;
  unsigned long lastDraw = 0;

  while (!appExitRequested()) {
    float ax = 0, ay = 0, az = 0;
    if (M5.Imu.getAccel(&ax, &ay, &az)) {
      // Low-pass filter to keep the bubble steady.
      smX = haveReading ? smX * 0.7f + ax * 0.3f : ax;
      smY = haveReading ? smY * 0.7f + ay * 0.3f : ay;
      smZ = haveReading ? smZ * 0.7f + az * 0.3f : az;
      haveReading = true;
    }

    if (M5.BtnA.wasClicked()) {
      previousMillis = millis();
      tareX = smX;
      tareY = smY;
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

      if (!haveReading) {
        M5.Lcd.setFont(&beta8pt7b);
        M5.Lcd.setCursor(10, (current_screen == STICKC) ? 45 : 77);
        M5.Lcd.print("NO IMU");
        delay(10);
        continue;
      }

      float ex = smX - tareX;
      float ey = smY - tareY;
      // Tilt of each axis from horizontal, in degrees.
      float pitch = atan2f(ex, sqrtf(ey * ey + smZ * smZ)) * 180.0f / PI;
      float roll = atan2f(ey, sqrtf(ex * ex + smZ * smZ)) * 180.0f / PI;

      // Bubble position, clamped inside the ring.
      float ox = constrain(ex, -1.0f, 1.0f) * (R - bubbleR);
      float oy = constrain(ey, -1.0f, 1.0f) * (R - bubbleR);
      float mag = sqrtf(ox * ox + oy * oy);
      if (mag > R - bubbleR) {
        ox *= (R - bubbleR) / mag;
        oy *= (R - bubbleR) / mag;
      }

      M5.Lcd.drawCircle(cx, cy, R, txt_color);
      M5.Lcd.drawCircle(cx, cy, R / 2, txt_color);
      M5.Lcd.drawLine(cx - R, cy, cx + R, cy, txt_color);
      M5.Lcd.drawLine(cx, cy - R, cx, cy + R, txt_color);
      M5.Lcd.fillCircle(cx + (int)ox, cy + (int)oy, bubbleR, txt_color);

      bool level = fabsf(pitch) < 2.0f && fabsf(roll) < 2.0f;
      M5.Lcd.setFont(&beta8pt7b);
      M5.Lcd.setCursor(textX, cy - 16);
      M5.Lcd.print("X " + String(pitch, 1));
      M5.Lcd.setCursor(textX, cy + 2);
      M5.Lcd.print("Y " + String(roll, 1));
      M5.Lcd.setFont(&beta5pt7b);
      M5.Lcd.setCursor(textX, cy + 20);
      M5.Lcd.print(level ? "LEVEL" : "BTNA: TARE");
    }

    delay(10);
  }
}

#endif
