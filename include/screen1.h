#define BLE_NOTICE_MS 900

// Type the code into the paired host and report what happened on screen.
void typeCodeOverBLE(const String& code) {
  M5.Lcd.fillRect(0, toolbar_height, screen_x, screen_y - toolbar_height, bg_color);
  M5.Lcd.setFont(&beta8pt7b);
  M5.Lcd.setCursor(10, (current_screen == STICKC) ? 45 : 77);

  if (!bleHidConnected()) {
    M5.Lcd.print("NO BT HOST");
// M5.Speaker.tone(2000, 250);
    delay(BLE_NOTICE_MS);
    return;
  }

  M5.Lcd.print("TYPING...");
// M5.Speaker.tone(5000, 100);
  bleHidTypeCode(code);
  delay(BLE_NOTICE_MS);
}

// Remaining seconds of the current 30s code window, bottom-right above the
// time bar. The old value is erased first (sized for the widest value,
// "30s") so a shrinking value leaves no remnants behind.
void drawRemainingSeconds() {
  M5.Lcd.setFont(&beta5pt7b);
  String rem = String(OTP_TIME_STEP - (second() % OTP_TIME_STEP)) + "s";
  int maxW = M5.Lcd.textWidth("30s");
  int y = screen_y - 15;
  M5.Lcd.fillRect(screen_x - 5 - maxW, y, maxW, 8, bg_color);
  M5.Lcd.setTextColor(txt_color, bg_color);
  M5.Lcd.setCursor(screen_x - 5 - M5.Lcd.textWidth(rem), y);
  M5.Lcd.print(rem);
}

void OTP_screen() {

  int how_many_otp_registred = 0;

  for (int i = 0; i < maxOTPs; i++) {
    String otpBool = "B" + String((i + 1));
    (NVS.getInt(otpBool) == 1) ? how_many_otp_registred++ : true;

  }

  int pointer = 0;               //point before first OTP code to activate the first search in memory

  bool firstloadScreen = true;   //true firstloadScreen

  String totpCode = "";

  int sec = -1;                    //will store last secend when the bar was updated

  // Cache the displayed code: the key only changes when the 30s time step
  // rolls over or another OTP is selected, so re-reading NVS and re-running
  // the HMAC on every loop iteration is wasted work. The cache lives only
  // for one visit to this screen, so a code can never survive a WebUI edit.
  long cachedStep = -1;
  int cachedPointer = -1;
  int cachedDigits = OTP_DIGITS_DEFAULT;
  String cachedCode = "";

  while (!appExitRequested()) {


    if (how_many_otp_registred == 0) {
      if (firstloadScreen) {

        // fill the screen with bg color, except the toolbar
        M5.Lcd.fillRect(0, toolbar_height, screen_x, screen_y - toolbar_height, bg_color);

        if (current_screen == STICKC) {
          M5.Lcd.setCursor(10, 32); 
        } else {
          M5.Lcd.setCursor(10, 32);
        }
        M5.Lcd.setFont(&beta5pt7b);
        M5.Lcd.print("NO OTPs");

        if (current_screen == STICKC) {
          M5.Lcd.setCursor(10, 43);
        } else {
          M5.Lcd.setCursor(10, 43);
        }
        M5.Lcd.setFont(&beta5pt7b);
        M5.Lcd.print("GO TO WIFI SETUP");

        if (current_screen == STICKC) {
          M5.Lcd.setCursor(10, 60);
        } else {
          M5.Lcd.setCursor(10, 60);
        }

        M5.Lcd.setFont(&beta8pt7b);
        M5.Lcd.print("Aiixi-toy");

        // if (current_screen == STICKC) {
        //    M5.Lcd.drawXBitmap(90, 17, logoapp_bits, logoapp_width, logoapp_height, BLACK, WHITE);
        // } else {
        //    M5.Lcd.drawXBitmap(90, 17, logoapp_bits, logoapp_width, logoapp_height, BLACK, WHITE);
        // }
       

        firstloadScreen = false;
      }
    } else {

      // short press cycles through the stored OTPs, a long press types the
      // displayed code over BLE, so the two must not both fire on one press
      if (M5.BtnA.wasClicked() | firstloadScreen) {
        bool search = true;
        previousMillis = millis();
        
        while (search) {                                         // Search in memory for valid OTP data
          (pointer == maxOTPs) ? pointer = 1 : pointer++;
          String otpBool = "B" + String(pointer);
          (NVS.getInt(otpBool) == 1) ? search = false : false;
          firstloadScreen = true;
        }
// M5.Speaker.tone(7000, 100);
      }

      // Re-read the key and regenerate only when the selection or the
      // time step changed; the long-press BLE typing below uses this same
      // value, so it always types the code of the current step.
      long currentStep = now() / OTP_TIME_STEP;
      if (pointer != cachedPointer || currentStep != cachedStep) {
        size_t hmac_length = NVS.getBlobSize(String(pointer));
        uint8_t hmacKey[hmac_length] = {};
        NVS.getBlob(String(pointer), hmacKey, hmac_length);
        cachedDigits = getOTPDigits(pointer);
        cachedCode = generateTOTP(hmacKey, hmac_length, currentStep * OTP_TIME_STEP, cachedDigits);
        cachedPointer = pointer;
        cachedStep = currentStep;
      }
      int otpDigits = cachedDigits;
      String newCode = cachedCode;

      if (M5.BtnA.wasHold()) {
        previousMillis = millis();
        typeCodeOverBLE(newCode);
        firstloadScreen = true;   // repaint the code once the notice clears
      }

      if (totpCode != newCode | firstloadScreen) {

        firstloadScreen = false;
        // fill the screen with black color, except the toolbar
        M5.Lcd.fillRect(0, toolbar_height, screen_x, screen_y - toolbar_height, bg_color);

        totpCode = String(newCode);

        if (current_screen == STICKC) { // set text for label
          M5.Lcd.setCursor(10, 18);
          M5.Lcd.setFont(&beta5pt7b);
        } else {
          M5.Lcd.setCursor(10, 31);
          M5.Lcd.setFont(&beta8pt7b);
        }
        
        String otpLabel = "L" + String(pointer);
        int charsNumber = 25;
        if (current_screen == STICKC) {
          charsNumber = 15;
        }
        M5.Lcd.print(String(pointer) + ": " + NVS.getString(otpLabel).substring(0, charsNumber));

        if (current_screen == STICKC) { // set text for user
          M5.Lcd.setCursor(10, 30);
        } else {
          M5.Lcd.setFont(&beta5pt7b);
          M5.Lcd.setCursor(10, 47);
        }

        String otpUser = "U" + String(pointer);
        M5.Lcd.print(NVS.getString(otpUser).substring(0, charsNumber));

        // 8 digit codes need a smaller font to stay inside the display width
        if (current_screen == STICKC) { // set text for code
          M5.Lcd.setCursor(10, 45);
          M5.Lcd.setFont((otpDigits > 6) ? &beta10pt7b : &beta15pt7b);
        } else {
          M5.Lcd.setCursor(10, 77);
          M5.Lcd.setFont((otpDigits > 6) ? &beta15pt7b : &mishmash21pt7b);
        }

        
        M5.Lcd.print(newCode);

        // Position among the stored OTPs, e.g. "3/12", top-right on the
        // label line.
        int order = 0;
        for (int i = 1; i <= pointer; i++) {
          if (NVS.getInt("B" + String(i)) == 1) {
            order++;
          }
        }
        String pos = String(order) + "/" + String(how_many_otp_registred);
        if (current_screen == STICKC) {
          M5.Lcd.setFont(&beta5pt7b);
          M5.Lcd.setCursor(screen_x - 5 - M5.Lcd.textWidth(pos), 18);
        } else {
          M5.Lcd.setFont(&beta8pt7b);
          M5.Lcd.setCursor(screen_x - 5 - M5.Lcd.textWidth(pos), 31);
        }
        M5.Lcd.setTextColor(txt_color, bg_color);
        M5.Lcd.print(pos);

        drawRemainingSeconds();
      }

      //-----------------------------------timebar----------------------------------------//
      if (sec != second()) {
        sec = second();
        int elapsed = second() % OTP_TIME_STEP;
        // Warn during the last 5 seconds of the period, judged by remaining
        // time rather than pixel position: the old fixed 130px threshold
        // could never trigger on the 80px-wide M5StickC screen.
        bool lastSeconds = (OTP_TIME_STEP - elapsed) <= 5;
        int OTP_bar = (int)(((float)elapsed / OTP_TIME_STEP) * screen_x);
        M5.Lcd.fillRect(0, screen_y - 3, OTP_bar, 3, lastSeconds ? RED : txt_color);
        M5.Lcd.fillRect(OTP_bar, screen_y - 3, screen_x - OTP_bar, 3, bg_color);
        M5.Power.setLed(lastSeconds ? 1 : 0);
        drawRemainingSeconds();
      }
    } //-----------------------------------timebar----------------------------------------//
  }
}
