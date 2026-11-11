// Device lock screen: PIN entry with the two physical buttons only.
//
// The PIN is stored in NVS under "pincode" and is set through the WebUI
// (/set_pin); this file only gates the on-device UI. BtnA enters digit 8,
// BtnB enters digit 7, so a PIN can only consist of the digits 7 and 8.
// PINs containing any other digit are rejected by /set_pin, and a stored
// PIN that is not enterable is skipped here (fail-safe) so the device can
// never be permanently locked out by an un-enterable PIN.

#ifndef PIN_LOCK_H
#define PIN_LOCK_H

// Digit entered by each physical button. Named constants so the mapping
// can be swapped in one place.
#define PIN_KEY_A_DIGIT 8
#define PIN_KEY_B_DIGIT 7

#define PIN_MIN_LENGTH 4
#define PIN_MAX_LENGTH 12
#define PIN_MAX_FAILURES 5
#define PIN_LOCKOUT_MS 60000UL

// True when the PIN can be entered with the two buttons: only digits 7
// and 8, length PIN_MIN_LENGTH..PIN_MAX_LENGTH.
bool pinIsEnterable(const String& pin) {
  if (pin.length() < PIN_MIN_LENGTH || pin.length() > PIN_MAX_LENGTH) {
    return false;
  }
  for (unsigned int i = 0; i < pin.length(); i++) {
    char c = pin.charAt(i);
    if (c != (char)('0' + PIN_KEY_A_DIGIT) && c != (char)('0' + PIN_KEY_B_DIGIT)) {
      return false;
    }
  }
  return true;
}

// Draw the lock screen: a title, one dot per entered digit (never the
// digits themselves), an optional message line, and the key mapping hint.
void pinLockDrawPrompt(const String& entered, int pinLength, const String& message) {
  M5.Lcd.fillScreen(bg_color);
  M5.Lcd.setTextColor(txt_color, bg_color);

  if (current_screen == STICKC) {
    M5.Lcd.setFont(&beta8pt7b);
    M5.Lcd.setCursor(5, 25);
  } else {
    M5.Lcd.setFont(&beta10pt7b);
    M5.Lcd.setCursor(5, 30);
  }
  M5.Lcd.print("Enter PIN");

  // Dots for the entered digits, sized to fit the screen width.
  int spacing = (screen_x - 20) / pinLength;
  if (spacing < 6) {
    spacing = 6;
  }
  int radius = spacing / 2 - 1;
  if (radius < 1) {
    radius = 1;
  }
  if (radius > 4) {
    radius = 4;
  }
  int totalWidth = spacing * (pinLength - 1);
  int startX = (screen_x - totalWidth) / 2;
  int dotY = (current_screen == STICKC) ? 50 : 65;
  for (int i = 0; i < pinLength; i++) {
    int x = startX + i * spacing;
    if (i < (int)entered.length()) {
      M5.Lcd.fillCircle(x, dotY, radius, txt_color);
    } else {
      M5.Lcd.drawCircle(x, dotY, radius, txt_color);
    }
  }

  if (message.length() > 0) {
    if (current_screen == STICKC) {
      M5.Lcd.setFont(&beta5pt7b);
      M5.Lcd.setCursor(5, 70);
    } else {
      M5.Lcd.setFont(&beta8pt7b);
      M5.Lcd.setCursor(5, 90);
    }
    M5.Lcd.print(message);
  }

  if (current_screen == STICKC) {
    M5.Lcd.setFont(&beta5pt7b);
    M5.Lcd.setCursor(5, 80);
  } else {
    M5.Lcd.setFont(&beta8pt7b);
    M5.Lcd.setCursor(5, 110);
  }
  M5.Lcd.print("A=8  B=7");
}

// Block in setup() until the device is unlocked. Returns immediately when
// no PIN is set, the device was already unlocked from the WebUI in this
// boot (pin_UNLOCK == pincode), or the stored PIN cannot be entered with
// the two buttons (fail-safe: skip locking instead of bricking the device).
void requireDeviceUnlock() {
  String pincode = NVS.getString("pincode");
  if (pincode == "") {
    return;
  }
  if (pin_UNLOCK == pincode) {
    return;
  }
  if (!pinIsEnterable(pincode)) {
    return;
  }

  String entered = "";
  int failures = 0;

  pinLockDrawPrompt(entered, pincode.length(), "");

  while (true) {
    M5.update();

    bool pressed = false;
    if (M5.BtnA.wasPressed()) {
      entered += (char)('0' + PIN_KEY_A_DIGIT);
      pressed = true;
    } else if (M5.BtnB.wasPressed()) {
      entered += (char)('0' + PIN_KEY_B_DIGIT);
      pressed = true;
    }

    if (!pressed) {
      delay(10);
      continue;
    }

    previousMillis = millis();

    if (entered.length() < pincode.length()) {
      pinLockDrawPrompt(entered, pincode.length(), "");
      delay(10);
      continue;
    }

    // The entered PIN reached the stored length: submit it.
    if (entered == pincode) {
      pin_UNLOCK = pincode;
      pinLockDrawPrompt(entered, pincode.length(), "Unlocked");
      delay(800);
      return;
    }

    failures++;
    entered = "";
    if (failures >= PIN_MAX_FAILURES) {
      // Lock out input for PIN_LOCKOUT_MS, showing the countdown.
      // Button presses during the lockout are ignored.
      unsigned long lockoutStart = millis();
      while (millis() - lockoutStart < PIN_LOCKOUT_MS) {
        M5.update();
        int remaining = (int)((PIN_LOCKOUT_MS - (millis() - lockoutStart) + 999) / 1000);
        pinLockDrawPrompt(entered, pincode.length(), "Locked " + String(remaining) + "s");
        delay(200);
      }
      failures = 0;
      pinLockDrawPrompt(entered, pincode.length(), "");
    } else {
      pinLockDrawPrompt(entered, pincode.length(), "Wrong PIN");
      delay(1000);
      pinLockDrawPrompt(entered, pincode.length(), "");
    }
    delay(10);
  }
}

#endif
