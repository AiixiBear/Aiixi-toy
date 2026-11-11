/*
    Aiixi-toy
   ------------------------------------------
       M5StickC Plus 2 lifestyle gadget
       TOTP authenticator + everyday apps
  -------------------------------------------
 */

// Go to WIFI settings and connect to the specified Access point. 
// Then go to http://192.168.4.1/ to configure

#include <Arduino.h>

#define maxOTPs 30                         // max OTPs the device can handle
//#define timeout_ScreenOn 180000            // The time after which the device shuts down after inactivity


String rondom_letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";      // This string contains the characters used to generate the wifi password

#include<WiFi.h>
#include<WebServer.h>
#include <WiFiClient.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <Update.h>
#include<WiFiAP.h>
#include<TOTP.h>
#include<TimeLib.h>
#include<ArduinoJson.h>
#include"ArduinoNvs.h"

// SPIFFS.h must come before M5Unified.h: M5GFX only enables its
// SPIFFS DataWrapper (used by drawJpgFile) when _SPIFFS_H_ is already
// defined at the time its headers are parsed.
#include <SPIFFS.h>
#include"M5Unified.h"

// Run-Time Variables //

WebServer server(80);

#include"beta15pt7b.h"
#include"beta10pt7b.h"
#include"beta8pt7b.h"
#include"beta5pt7b.h"
#include"Mishmash21pt7b.h"

#include"variable_runtime.h"
#include"weather_cfg.h"
#include"totp_plus.h"
#include"ble_hid.h"
#include"pin_lock.h"
#include"index.h"
#include"css.h"
#include"favico.h"

#include"screen.h"
#include"screen1.h"
#include"screen2.h"
#include"photo_store.h"
#include"memo_cfg.h"
#include"uptime_cfg.h"
#include"screen3.h"
#include"screen4.h"
#include"screen5.h"
#include"net.h"
#include"app_weather.h"
#include"app_air.h"
#include"app_media.h"
#include"app_uptime.h"
#include"app_level.h"
#include"app_db.h"
#include"app_qr.h"
#include"app_photo.h"
#include"app_memo.h"
#include"apps.h"
#include"math.h"

void setup() {

  Serial.begin(115200);
  while (!Serial);

  auto cfg = M5.config();
  M5.begin(cfg);

  NVS.begin();

  // Photo frame storage (128 KB SPIFFS). Formats on first boot, when
  // the partition is still empty.
  photoFsBegin();

  // OTP entries stored before digit lengths were configurable have no "D" key;
  // pin them to the 6 digit codes they were generated with.
  if (NVS.getInt("digits_migr") != 1) {
    for (int i = 1; i <= maxOTPs; i++) {
      String otpBool = "B" + String(i);
      String otpDigits = "D" + String(i);
      if (NVS.getInt(otpBool) == 1 && NVS.getInt(otpDigits) == 0) {
        NVS.setInt(otpDigits, OTP_DIGITS_DEFAULT);
      }
    }
    NVS.setInt("digits_migr", 1);
  }

  Serial.println("===============Aiixi-toy===============");
  Serial.println("================= V 1.2 ================");
  Serial.println("===============Aiixi-toy===============");

  // -- Get Preferences from NVS
  if (NVS.getInt("lcd_brightness") != 0) {
    lcd_brightness = NVS.getInt("lcd_brightness");
  }
  M5.Display.setBrightness(lcd_brightness); // 0 - 255

  // get timeout from NVS
  if (NVS.getInt("timeout_ScreenOn") != 0) {
    timeout_ScreenOn = NVS.getInt("timeout_ScreenOn");
  }

  // get timezone from NVS
  timezone_h = NVS.getInt("timezone_h");
  timezone_m = NVS.getInt("timezone_m");

  // get bg and txt color from NVS
  if (NVS.getInt("bg_color") != 0) {
    bg_color = NVS.getInt("bg_color");
  }
  if (NVS.getInt("txt_color") != 0) {
    txt_color = NVS.getInt("txt_color");
  }

  M5.Lcd.setRotation(1);
  screen_x = M5.Lcd.width();
  screen_y = M5.Lcd.height();

  #ifdef ARDUINO_M5STICK_C
    current_screen = STICKC;
  #endif

  #ifdef ARDUINO_M5STICK_C_PLUS_2
    current_screen = STICKCPLUS2;
  #endif
  

  M5.Lcd.fillScreen(bg_color);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(txt_color, bg_color);

  // start the BLE HID keyboard so codes can be typed into a paired host
  bleHidBegin();

  // get tm from RTC
  tm rtc_tm = M5.Rtc.getDateTime().get_tm();
  setTime(rtc_tm.tm_hour, rtc_tm.tm_min, rtc_tm.tm_sec, rtc_tm.tm_mday, rtc_tm.tm_mon + 1, rtc_tm.tm_year + 1900);

  // Check if time may have drifted (last sync > 15 days ago)
  int last_synced = NVS.getInt("last_time_synced", 0);
  if (last_synced != 0) {
    const long fifteen_days_sec = 15L * 24 * 3600;
    long diff = (long)now() - (long)last_synced;
    if (diff < 0) diff = -diff;
    if (diff > fifteen_days_sec) {
      M5.Lcd.fillScreen(bg_color);
      M5.Lcd.setTextColor(txt_color, bg_color);
      M5.Lcd.setFont(&beta8pt7b);
      M5.Lcd.setCursor(5, 25);
      M5.Lcd.print("TIME MAY HAVE");
      M5.Lcd.setCursor(5, 45);
      M5.Lcd.print("DRIFTED!");
      while (true) {
        M5.update();
        if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed()) break;
      }
    }
  }

  // If a PIN is set, block here until it is entered on the device.
  // No PIN set (or an un-enterable one) means no lock screen.
  requireDeviceUnlock();

  // App enable/disable endpoints (registered here because apps.h, where
  // the registry lives, is included after screen3.h).
  registerAppEndpoints();

}

void loop() {

// The home screen is the root: it lists the apps, enters the selected
// one, and returns to the list when the user leaves an app.
  home_screen();

}

