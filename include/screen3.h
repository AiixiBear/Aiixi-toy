// DNS Server for Captive Portal
#include <time.h>
#include <esp_sntp.h>

DNSServer dnsServer;
const byte DNS_PORT = 53;
bool _dnsStarted = false;

// Captive portal redirect handler
void handleCaptivePortal() {
  previousMillis = millis();
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

// Serve a simple redirect page for captive portal detection
void handleCaptiveDetect() {
  previousMillis = millis();
  String html = "<!DOCTYPE html><html><head><meta http-equiv='refresh' content='0;url=http://192.168.4.1/'></head><body><a href='http://192.168.4.1/'>Click here to configure OXOTP</a></body></html>";
  server.send(200, "text/html", html);
}

// Android captive portal detection (expects 204 response, we send redirect instead)
void handleGenerate204() {
  previousMillis = millis();
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

// NTP time sync, only usable in STA mode (AP mode has no uplink).
// Bounded: tries a few times, then gives up without blocking the WiFi
// screen. On failure the manual sync paths (WebUI /unix, drift screen)
// are untouched, and the previously held time is kept.
// Note: getLocalTime alone cannot prove a sync happened, because it also
// succeeds while the RTC still holds a plausible old time. The SNTP sync
// status is checked instead, so last_time_synced is only updated when the
// time actually came from an NTP server.
#define NTP_SYNC_ATTEMPTS 3
#define NTP_SYNC_WAIT_MS 2000

bool syncTimeFromNTP() {
  long gmtOffset_sec = (long)timezone_h * 3600 + (long)timezone_m * 60;
  configTime(gmtOffset_sec, 0, "pool.ntp.org", "time.google.com");

  for (int attempt = 0; attempt < NTP_SYNC_ATTEMPTS; attempt++) {
    unsigned long start = millis();
    while (millis() - start < NTP_SYNC_WAIT_MS) {
      if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        time_t epoch = time(nullptr);
        // Reject implausible results instead of trusting them.
        if (epoch < 1700000000L) {
          break;
        }
        tm* utc_tm = gmtime(&epoch);
        M5.Rtc.setDateTime(utc_tm);
        setTime(epoch);
        NVS.setInt("last_time_synced", (int)epoch);
        Serial.println("NTP time sync OK");
        return true;
      }
      delay(100);
    }
  }

  Serial.println("NTP time sync failed, keeping previous time");
  return false;
}

// True when s is a decimal number: optional '-', digits, at most one '.'.
// toFloat() cannot validate on its own: it silently returns 0.0 for
// non-numeric input.
bool isDecimalString(const String& s) {
  if (s.length() == 0) {
    return false;
  }
  unsigned int i = 0;
  if (s.charAt(0) == '-') {
    if (s.length() == 1) {
      return false;
    }
    i = 1;
  }
  bool dotSeen = false;
  bool digitSeen = false;
  for (; i < s.length(); i++) {
    char c = s.charAt(i);
    if (c == '.') {
      if (dotSeen) {
        return false;
      }
      dotSeen = true;
    } else if (c >= '0' && c <= '9') {
      digitSeen = true;
    } else {
      return false;
    }
  }
  return digitSeen;
}

void Wifi_screen() {

  // When the AP password was printed on screen, and whether it has been
  // masked since. The password is shown in clear text for 5s, then
  // replaced with asterisks: it would otherwise stay readable by anyone
  // nearby for as long as the WiFi screen is open.
  unsigned long wifiPassShownAt = 0;
  bool wifiPassMasked = false;

  // -------  GET THE WIFI SETTINGS --------- //

  // generate SSID for wifi AP
  char AP_ssid[13];   
  String macaddr = String(WiFi.macAddress());
  macaddr.replace(":", "");
  macaddr = macaddr.substring(6);
  String ssid_mac = "OXOTP_" + macaddr;
  ssid_mac.toCharArray(AP_ssid, 13);


  //----- generate password for wifi
  // Arduino random() is backed by the ESP32 hardware RNG (esp_random)
  // unless randomSeed() was called, which this project never does, so no
  // extra seeding is needed. random(min, max) returns [min, max), so the
  // upper bound must be the full length for every character to be usable.
  String pass_gen = "";
  for (int i = 0; i < 12; i++) {
    pass_gen += rondom_letters[random(0, rondom_letters.length())];
  }
  
  String pass_static = ""; // Default password
  pass_static = NVS.getString("wifi_password");
  if (pass_static == "") {
    pass_static = "1A2B3C4D5E"; // fallback to an unsecure password
  }

  //0 : random password mode, 1 : static password mode
  passwordMode = NVS.getInt("passwordMode");
  Wifi_Mode = NVS.getString("wifiMode"); // Default mode

  if (Wifi_Mode == "") {
    Wifi_Mode = "AP";   // if the mode is not set, default to AP mode
  }

  if (Wifi_Mode == "AP") {
    Wifi_SSID = ssid_mac;
  } else {
    Wifi_SSID = NVS.getString("wifi_ssid");
  }
  
  // get password mode settings
  if (passwordMode == 0 && Wifi_Mode == "AP") {
    Serial.println("Using random password mode");
    Wifi_PASSWORD = pass_gen;
  } else {
    Serial.println("Using static password mode");
    Wifi_PASSWORD = pass_static;
  }

  char password[Wifi_PASSWORD.length() + 1];
  Wifi_PASSWORD.toCharArray(password, Wifi_PASSWORD.length() + 1);


  // clear the screen, except the toolbar area
  M5.Lcd.fillRect(0, toolbar_height, screen_x, screen_y - toolbar_height, bg_color);
  show_wait_icon();
  

  //-------  START WIFI  --------- //
  if (Wifi_Mode == "AP") {
    Serial.println("Starting in AP mode");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_ssid, password);
    
    // Start DNS server for captive portal (redirect all DNS to this IP)
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    _dnsStarted = true;
    Serial.println("Captive portal DNS started");
  } 
  else if (Wifi_Mode != "AP" && Wifi_SSID != "") { 
    Serial.println("Starting in STA mode");
    WiFi.mode(WIFI_STA);
    WiFi.begin(Wifi_SSID, password);
    Serial.println("Try Connecting to " + Wifi_SSID);

    // Wait for the connection to complete
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED) {
      delay(250);
      // If the connection doesn't complete within 10 seconds, break the loop
      if (millis() - startTime > 10000) {
        break;
      }
    }
    Serial.println("Connection status: " + String(WiFi.status()));
    // print the ip address in the serial monitor and in the screen
    Serial.println("Connected to " + Wifi_SSID + " with IP: " + WiFi.localIP().toString());



  }
  

  // If the connection failed, fallback to AP mode
  if (Wifi_Mode != "AP" && WiFi.status() != WL_CONNECTED) {
    Serial.println("Failed to connect to the specified network. Falling back to AP mode.");
    Wifi_SSID = ssid_mac;
    Wifi_Mode = "AP";
    Wifi_PASSWORD = pass_gen; // generate a new password for fallback AP mode
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_ssid, pass_gen);
    Serial.println("AP mode started (fallback)");
    
    // Start DNS server for captive portal
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    _dnsStarted = true;
    Serial.println("Captive portal DNS started (fallback)");
  }

  // STA mode with a working uplink: correct the RTC from NTP. AP mode has
  // no uplink, and a failed sync simply keeps the previous time.
  if (Wifi_Mode != "AP" && WiFi.status() == WL_CONNECTED) {
    syncTimeFromNTP();
  }

    // use mdns to resolve the hostname
  if (MDNS.begin("oxotp")) { //http://esp32.local
    Serial.println("mDNS responder started");
    _mdnsStarted = true;
  }
  else {
      Serial.println("Error setting up MDNS responder!");
    _mdnsStarted = false;
  }


  // clear the wait icon
  clear_wait_icon();

  // print the wifi settings in the screen
  if (current_screen == STICKC) {
    M5.Lcd.setFont(&beta8pt7b);
    M5.Lcd.setCursor(10, 15);
  } else {
    M5.Lcd.setFont(&beta10pt7b);
    M5.Lcd.setCursor(9, 36);
  }
  M5.Lcd.print ("CONNECT TO:");


  if (current_screen == STICKC) {
    M5.Lcd.setFont(&beta5pt7b);
    M5.Lcd.setCursor(10, 30);
  } else {
    M5.Lcd.setFont(&beta8pt7b);
    M5.Lcd.setCursor(10, 60);
  }
   M5.Lcd.print (Wifi_SSID);

    if (current_screen == STICKC) {
      M5.Lcd.setFont(&beta5pt7b);
      M5.Lcd.setCursor(10, 43);
    } else {
      M5.Lcd.setFont(&beta8pt7b);
      M5.Lcd.setCursor(10, 90);
    }

  // we don't want to show the password in the screen if not in AP mode
  // only for M5StickC Plus 2
  if (Wifi_Mode == "AP") {
    M5.Lcd.print (Wifi_PASSWORD);
    wifiPassShownAt = millis();

    if (current_screen == STICKCPLUS2) {
      M5.Lcd.setFont(&beta5pt7b);
      M5.Lcd.setCursor(10, 110);
      M5.Lcd.print("Auto-popup on connect");
    }
  } else {
    if (_mdnsStarted) {
      M5.Lcd.print ("goto: OXOTP.local");
    } else {
      M5.Lcd.print (WiFi.localIP().toString());
    }
  }


  //----- start server
  server.enableCORS();
  server.enableCrossOrigin();

  // ---- Captive Portal Detection URLs ----
  // These URLs are checked by devices to detect captive portals
  
  // Android
  server.on("/generate_204", HTTP_GET, handleGenerate204);
  server.on("/gen_204", HTTP_GET, handleGenerate204);
  server.on("/connecttest.txt", HTTP_GET, handleCaptiveDetect);  // Windows
  server.on("/ncsi.txt", HTTP_GET, handleCaptiveDetect);  // Windows
  
  // iOS / Apple
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptiveDetect);
  server.on("/library/test/success.html", HTTP_GET, handleCaptiveDetect);
  
  // Firefox
  server.on("/success.txt", HTTP_GET, handleCaptiveDetect);
  server.on("/canonical.html", HTTP_GET, handleCaptiveDetect);
  
  // Catch-all for any unhandled requests (redirect to main page)
  server.onNotFound([]() {
    // If it's a captive portal check or unknown URL, redirect to main page
    if (Wifi_Mode == "AP") {
      server.sendHeader("Location", "http://192.168.4.1/", true);
      server.send(302, "text/plain", "");
    } else {
      server.send(404, "text/plain", "Not Found");
    }
  });

  // Route for serving the zipped HTML page
  server.on("/", HTTP_GET, []() {
    previousMillis = millis();
    server.sendHeader("Content-Encoding", "gzip");
    server.send_P(200, "text/html", index_html_gz, sizeof(index_html_gz));
  });

  // Route to load style.css file
  server.on("/css/tailwind.min.css", HTTP_GET, []() {
    server.sendHeader("Content-Encoding", "gzip");
    server.send_P(200, "text/css", css_gz, sizeof(css_gz));
  });

    // Route to load the favicon.ico file
  server.on("/favicon.png", HTTP_GET, []() {
    server.sendHeader("Content-Encoding", "gzip");
    server.send_P(200, "image/png", favico_gz, sizeof(favico_gz));
  });

  server.handleClient();

  //----------------add-------------------

  // preflight OPTIONS request for the /add endpoint
  server.on("/add", HTTP_OPTIONS, []() {
    server.send(200);
  });
  server.on("/add", HTTP_POST, []() {
    jsondata.clear();
    previousMillis = millis();
    String pincode = NVS.getString ("pincode");
    if (pin_UNLOCK == pincode) {

      deserializeJson(jsondata, server.arg(0));
      int hmac_length = jsondata["hmac_length"];
      uint8_t hmacKey[hmac_length] = {};

      for (int i = 0; i < hmac_length; i++) {
        hmacKey[i] = jsondata["data"][i];
      }

      int id = jsondata["id"];

      String otpBool = jsondata["id"];
      otpBool = "B" + otpBool;

      if (NVS.getInt (otpBool) == 0) {

        NVS.setInt (otpBool, 1);

        String otpUser = jsondata["id"];
        otpUser = "U" + otpUser;
        NVS.setString (otpUser, jsondata["user"]);

        String otpLabel = jsondata["id"];
        otpLabel = "L" + otpLabel;
        NVS.setString (otpLabel, jsondata["label"]);

        String otpDigits = jsondata["id"];
        otpDigits = "D" + otpDigits;
        NVS.setInt (otpDigits, normalizeOTPDigits(jsondata["digits"] | OTP_DIGITS_DEFAULT));

        NVS.setBlob(jsondata["id"], hmacKey, hmac_length);

        server.send(200, " text/html", "OK");

      } else {
        server.send(200, " text/html", "ALREADY EXIST");
      }
    } else {
      server.send(200, " text/html", "LOCKED");
    }
  });


  //----------------delete-------------------

  server.on("/delete", HTTP_OPTIONS, []() {
    server.send(200);
  });

  server.on("/delete", HTTP_POST, []() {
    previousMillis = millis();
    jsondata.clear();

    String pincode = NVS.getString ("pincode");
    if (pin_UNLOCK == pincode) {
      deserializeJson(jsondata, server.arg(0));
      int id = jsondata["id"];

      String  otpBool = jsondata["id"];
      otpBool = "B" + otpBool;

      String otpUser = jsondata["id"];
      otpUser = "U" + otpUser;

      String otpLabel = jsondata["id"];
      otpLabel = "L" + otpLabel;

      String otpDigits = jsondata["id"];
      otpDigits = "D" + otpDigits;

      NVS.erase(otpBool);
      NVS.erase(otpLabel);
      NVS.erase(otpUser);
      NVS.erase(otpDigits);
      NVS.erase(String(id));

      // Reply only after the entry has been removed, so the client never
      // sees a success for a delete that did not happen.
      server.send(200, " text/html", "OK");
    } else {
      server.send(200, " text/html", "LOCKED");
    }
  });


  //----------------factoryReset--------------

  // Handle preflight OPTIONS 
  server.on("/factoryReset", HTTP_OPTIONS, []() {
    server.send(200);
  });

  server.on("/factoryReset", HTTP_POST, []() {
    jsondata.clear();
    previousMillis = millis();
    String pincode = NVS.getString ("pincode");
    if (pin_UNLOCK == pincode) {
      deserializeJson(jsondata, server.arg(0));
      String  data1 = jsondata["data"];

      server.send(200, " text/html", "OK");
      NVS.eraseAll();
      ESP.restart();
    } else {
      server.send(200, " text/html", "LOCKED");
    }
  });



  //----------------check_pin_secure--------------       check if there is a pin in OXOTP
  // handle preflight OPTIONS request
  server.on("/check_pin_secure", HTTP_OPTIONS, []() {
    server.send(200);
  });

  server.on("/check_pin_secure", HTTP_POST, []() {
    previousMillis = millis();
    jsondata.clear();
    String  pincode = NVS.getString ("pincode");

    if (pincode == "") {
      server.send(200, " text/html", "NOT");
    } else {      
      server.send(200, " text/html", "OK");
    }
  });



  //----------------check_pin_LOCK--------------         check if OXOTP is LOCKED
  // handle preflight OPTIONS request
  server.on("/check_pin_LOCK", HTTP_OPTIONS, []() {
    server.send(200);
  });
  server.on("/check_pin_LOCK", HTTP_POST, []() {
    previousMillis = millis();
    jsondata.clear();
    String  pincode = NVS.getString ("pincode");

    if (pin_UNLOCK == pincode) {
      server.send(200, " text/html", "UNLOCKED");
    } else {
      server.send(200, " text/html", "LOCKED");
    }
  });


  //----------------set_pin--------------------           set pin to OXOTP
  server.on("/set_pin", HTTP_OPTIONS, []() {
    server.send(200);
  });
  server.on("/set_pin", HTTP_POST, []() {
    previousMillis = millis();
    jsondata.clear();
    deserializeJson(jsondata, server.arg(0));
    String  pin = jsondata["newpin"];
    String  pincode = NVS.getString ("pincode");

    if (pin_UNLOCK == pincode) {
      // The device only has two buttons (BtnA = 8, BtnB = 7), so a PIN
      // with any other digit could never be entered on the device.
      // Reject it here instead of writing an un-enterable PIN to NVS.
      if (!pinIsEnterable(pin)) {
        server.send(200, " text/html", "INVALID PIN");
        return;
      }
      pin_UNLOCK = pin;
      NVS.setString ("pincode", pin);
      server.send(200, " text/html", "OK");
    } else {
      server.send(200, " text/html", "LOCKED");
    }


  });
  //----------------UNLOCK--------------                UNLOCK the OXOTP
  // handle preflight OPTIONS request
  server.on("/UNLOCK", HTTP_OPTIONS, []() {
    server.send(200);
  });
  server.on("/UNLOCK", HTTP_POST, []() {
    previousMillis = millis();
    jsondata.clear();
    deserializeJson(jsondata, server.arg(0));
    String  pin = jsondata["pin"];
    String  pincode = NVS.getString ("pincode");

    if (pin == pincode) {
      pin_UNLOCK = pin;
      server.send(200, " text/html", "OK");
    } else {
      server.send(200, " text/html", "LOCKED");
    }
  });


  //----------------unix--------------                 set time with UNIX
  // handle preflight OPTIONS request
  server.on("/unix", HTTP_OPTIONS, []() {
    server.send(200);
  });
  server.on("/unix", HTTP_POST, []() {
    previousMillis = millis();
    jsondata.clear();
    String pincode = NVS.getString ("pincode");
    if (pin_UNLOCK == pincode) {

      deserializeJson(jsondata, server.arg(0));
      time_t unix = jsondata["unix"];
      setTime(unix);

      // from unix to rtc_datetime_t
      tm *tm_struct = gmtime(&unix);

      M5.Rtc.setDateTime(tm_struct);

      // Store last sync time for drift detection
      NVS.setInt("last_time_synced", (int)unix);

      server.send(200, "text/html", "OK");
    } else {
      server.send(200, "text/html", "LOCKED");
    }
  });

  // get timezone
  server.on("/timezone", HTTP_GET, []() {
    previousMillis = millis();
    jsondata.clear();
    // String  pincode = NVS.getString ("pincode");
    // if (pin_UNLOCK != pincode) {
    //   server.send(200, "text/html", "LOCKED");
    //   return;
    // }

    // return the timezone value from timezone_h and timezone_m
    float _timezone = timezone_h + (timezone_m / 60.0);

    jsondata["timezone"] = _timezone;

    String dataout = "";
    serializeJson(jsondata, dataout);

    server.send(200, "text/html", dataout);
  });

  // set the timezone

  server.on("/timezone", HTTP_OPTIONS, []() {
    server.send(200);
  });

  server.on("/timezone", HTTP_POST, []() {
    previousMillis = millis();
    jsondata.clear();

    // Same PIN check as the other write endpoints; the GET handler stays
    // open so the page can read the timezone before unlocking.
    String pincode = NVS.getString ("pincode");
    if (pin_UNLOCK != pincode) {
      server.send(200, "text/html", "LOCKED");
      return;
    }

    deserializeJson(jsondata, server.arg(0)); 

    // set timezone_h and timezone_m from the float value
    float _timezone = jsondata["timezone"];

    if (_timezone < -14 || _timezone > 14) {
      server.send(200, "text/html", "INVALID");
      return;
    }

    timezone_h = static_cast<int>(_timezone);
    timezone_m = static_cast<int>((_timezone - timezone_h) * 60);

    // save the timezone value to the NVS
    NVS.setInt("timezone_h", timezone_h);
    NVS.setInt("timezone_m", timezone_m);

    server.send(200, "text/html", "OK");
  });


  //----------------getOTPs--------------              get the OXOTP list
  server.on("/getOTPs", HTTP_GET, []() {

    previousMillis = millis();
    String  pincode = NVS.getString ("pincode");
    if (pin_UNLOCK != pincode) {
      server.send(200, "text/html", "LOCKED");
      return;
    }

    jsondata.clear();
    int  json_counter = 0;
    JsonArray data = jsondata["OTPs"].to<JsonArray>();

    for (int  i = 0; i < maxOTPs; i++) {
      String  otpBool = "B" + String ((i + 1));
      int  id = NVS.getInt (otpBool);

      if (id == 1) {
        jsondata["OTPs"][json_counter]["id"] = (i + 1);
        String  otpLabel = "L" + String ((i + 1));
        jsondata["OTPs"][json_counter]["L"] = NVS.getString (otpLabel);
        String  otpUser = "U" + String ((i + 1));
        jsondata["OTPs"][json_counter]["U"] = NVS.getString (otpUser);
        jsondata["OTPs"][json_counter]["D"] = getOTPDigits (i + 1);
        json_counter++;
      }
    }

    String  dataout = "";
    serializeJson(jsondata, dataout);

    server.send(200, "text/html", dataout);
  });

  //----------------getCodes--------------              get the current TOTP codes
  // Codes are generated on the device: the secrets never leave it, the WebUI
  // only receives the codes for the current 30 second window plus the number
  // of seconds left in that window.
  server.on("/getCodes", HTTP_GET, []() {
    previousMillis = millis();
    String  pincode = NVS.getString ("pincode");
    if (pin_UNLOCK != pincode) {
      server.send(200, "text/html", "LOCKED");
      return;
    }

    jsondata.clear();
    int  json_counter = 0;
    JsonArray data = jsondata["codes"].to<JsonArray>();

    for (int  i = 0; i < maxOTPs; i++) {
      String  otpBool = "B" + String ((i + 1));
      if (NVS.getInt (otpBool) != 1) {
        continue;
      }

      size_t hmac_length = NVS.getBlobSize(String(i + 1));
      if (hmac_length == 0) {
        continue;
      }

      uint8_t hmacKey[hmac_length] = {};
      NVS.getBlob(String(i + 1), hmacKey, hmac_length);
      String code = generateTOTP(hmacKey, hmac_length, now(), getOTPDigits(i + 1));

      jsondata["codes"][json_counter]["id"] = (i + 1);
      jsondata["codes"][json_counter]["code"] = code;
      json_counter++;
    }

    jsondata["remaining"] = 30 - (second() % 30);

    String  dataout = "";
    serializeJson(jsondata, dataout);

    server.send(200, "text/html", dataout);
  });

  // -----------------setWifi-----------------            set the wifi settings
  server.on("/setWifi", HTTP_OPTIONS, []() {
    server.send(200);
  });

server.on("/setWifi", HTTP_POST, []() {
    previousMillis = millis();
    String  pincode = NVS.getString ("pincode");
    if (pin_UNLOCK != pincode) {
      server.send(200, "text/html", "LOCKED");
      return;
    }
    jsondata.clear();
    deserializeJson(jsondata, server.arg(0));

    if (!jsondata["mode"].isNull()) {
        String _mode = jsondata["mode"];
        NVS.setString("wifiMode", _mode);
        Wifi_Mode = _mode;
    }

    if (!jsondata["ssid"].isNull()) {
        String _ssid = jsondata["ssid"];
        NVS.setString("wifi_ssid", _ssid);
        Wifi_SSID = _ssid;
    }

    if (!jsondata["password"].isNull()) {
        String _pass = jsondata["password"];
        NVS.setString("wifi_password", _pass);
        Wifi_PASSWORD = _pass;
    }

    if (!jsondata["passwordMode"].isNull()) {
        int _passMode = jsondata["passwordMode"];
        NVS.setInt("passwordMode", _passMode);
        passwordMode = _passMode;
    }

    server.send(200, "text/html", "OK");
});

// endpoint for get the current wifi settings (mode, ssid, passwordMode)
  server.on("/getWifi", HTTP_GET, []() {
      previousMillis = millis();
      String  pincode = NVS.getString ("pincode");
      if (pin_UNLOCK != pincode) {
        server.send(200, "text/html", "LOCKED");
        return;
      }

      jsondata.clear();

      jsondata["mode"] = NVS.getString("wifiMode");
      jsondata["ssid"] = NVS.getString("wifi_ssid");
      jsondata["passwordMode"] = NVS.getInt("passwordMode", 1);

      String dataout = "";
      serializeJson(jsondata, dataout);

      server.send(200, "text/html", dataout);
  });

// Get Battery Level and Charging Status
  server.on("/getBattery", HTTP_GET, []() {
    previousMillis = millis();
    jsondata.clear();

    float voltage = M5.Power.getBatteryVoltage();
    float percentage = M5.Power.getBatteryLevel();
    bool charging = M5.Power.isCharging();

    jsondata["voltage"] = voltage;
    jsondata["percentage"] = percentage;
    jsondata["charging"] = charging;

    String dataout = "";
    serializeJson(jsondata, dataout);

    server.send(200, "text/html", dataout);
  });

// Set the LCD Brightness and timeout
  server.on("/setScreen", HTTP_OPTIONS, []() {
    server.send(200);
  });

  server.on("/setScreen", HTTP_POST, []() {
    previousMillis = millis();
    jsondata.clear();

    // Same PIN check as the other write endpoints.
    String pincode = NVS.getString ("pincode");
    if (pin_UNLOCK != pincode) {
      server.send(200, "text/html", "LOCKED");
      return;
    }

    deserializeJson(jsondata, server.arg(0));

    if (!jsondata["brightness"].isNull()) {
      int _brightness = jsondata["brightness"];
      // clamp the brightness value between 10 and 255
      _brightness = constrain(_brightness, 10, 255);
      M5.Lcd.setBrightness(_brightness);
      lcd_brightness = _brightness;

      // save the brightness value to the NVS
      NVS.setInt("lcd_brightness", _brightness);
    }

    if (!jsondata["timeout"].isNull()) {
      int _timeout = jsondata["timeout"];
      if (_timeout <= 0) {
        _timeout = -1;
      }
      else {
        _timeout = _timeout * 1000;
      }

      timeout_ScreenOn = _timeout;
      NVS.setInt("timeout_ScreenOn", _timeout);
      Serial.println("timeout_ScreenOn: " + String(timeout_ScreenOn));
    }

    // Colors are independent of the timeout: previously they were nested
    // inside the timeout block and silently ignored when no timeout was sent.
    if (!jsondata["bg_color"].isNull()) {
      bg_color = jsondata["bg_color"];
      NVS.setInt("bg_color", bg_color);
    }

    if (!jsondata["txt_color"].isNull()) {
      txt_color = jsondata["txt_color"];
      NVS.setInt("txt_color", txt_color);
    }

    if (!jsondata["bg_color"].isNull() || !jsondata["txt_color"].isNull()) {
      M5.Lcd.fillScreen(bg_color);
      M5.Lcd.setTextColor(txt_color, bg_color);
    }

    server.send(200, "text/html", "OK");
  });

  // Get the current LCD Brightness and timeout in seconds
  server.on("/getScreen", HTTP_GET, []() {
    previousMillis = millis();
    jsondata.clear();

    jsondata["brightness"] = lcd_brightness;
    int _timeout = timeout_ScreenOn == UINT32_MAX ? 0 : timeout_ScreenOn / 1000;
    jsondata["timeout"] = timeout_ScreenOn / 1000;

    jsondata["bg_color"] = bg_color;
    jsondata["txt_color"] = txt_color;

    String dataout = "";
    serializeJson(jsondata, dataout);

    server.send(200, "text/html", dataout);
  });

  // Get the weather location settings (name, latitude, longitude).
  server.on("/getWeatherCfg", HTTP_GET, []() {
    previousMillis = millis();
    String pincode = NVS.getString("pincode");
    if (pin_UNLOCK != pincode) {
      server.send(200, "text/html", "LOCKED");
      return;
    }
    jsondata.clear();
    jsondata["name"] = wxGetName();
    jsondata["lat"] = wxGetLat();
    jsondata["lon"] = wxGetLon();
    String dataout = "";
    serializeJson(jsondata, dataout);
    server.send(200, "text/html", dataout);
  });

  // Set the weather location settings. Latitude/longitude are validated
  // as decimal numbers in range; toFloat() alone cannot do this, because
  // it silently returns 0.0 for non-numeric input.
  server.on("/setWeatherCfg", HTTP_OPTIONS, []() {
    server.send(200);
  });

  server.on("/setWeatherCfg", HTTP_POST, []() {
    previousMillis = millis();
    String pincode = NVS.getString("pincode");
    if (pin_UNLOCK != pincode) {
      server.send(200, "text/html", "LOCKED");
      return;
    }
    jsondata.clear();
    deserializeJson(jsondata, server.arg(0));
    if (jsondata["name"].isNull() || jsondata["lat"].isNull() ||
        jsondata["lon"].isNull()) {
      server.send(200, "text/html", "INVALID");
      return;
    }
    String name = jsondata["name"].as<String>();
    String lat = jsondata["lat"].as<String>();
    String lon = jsondata["lon"].as<String>();
    float latF = lat.toFloat();
    float lonF = lon.toFloat();
    if (name == "" || !isDecimalString(lat) || !isDecimalString(lon) ||
        latF < -90 || latF > 90 || lonF < -180 || lonF > 180) {
      server.send(200, "text/html", "INVALID");
      return;
    }
    NVS.setString(WX_KEY_NAME, name.substring(0, 24));
    NVS.setString(WX_KEY_LAT, lat);
    NVS.setString(WX_KEY_LON, lon);
    server.send(200, "text/html", "OK");
  });

  // -- PHOTO FRAME API --
  // Photos are uploaded as multipart form data and stored in SPIFFS
  // (see photo_store.h for the limits). The upload handler mirrors the
  // OTA update handler above; the response handler reports the error
  // the upload handler recorded, if any.

  static File photoUploadFile;
  static String photoUploadPath = "";
  static String photoUploadError = "";
  static size_t photoUploadSize = 0;
  static bool photoUploadCheckedMagic = false;

  // List the stored photos with size and dimensions (w/h are 0 when
  // the file is not a decodable baseline JPEG).
  server.on("/getPhotos", HTTP_GET, []() {
    previousMillis = millis();
    String pincode = NVS.getString("pincode");
    if (pin_UNLOCK != pincode) {
      server.send(200, "text/html", "LOCKED");
      return;
    }
    jsondata.clear();
    JsonArray list = jsondata["photos"].to<JsonArray>();
    String names[PHOTO_MAX_COUNT];
    int n = photoList(names, PHOTO_MAX_COUNT);
    for (int i = 0; i < n; i++) {
      JsonObject p = list.add<JsonObject>();
      String path = String(PHOTO_DIR) + "/" + names[i];
      File f = SPIFFS.open(path);
      p["name"] = names[i];
      p["size"] = f ? (int)f.size() : 0;
      if (f) {
        f.close();
      }
      int w = 0, h = 0;
      if (photoDimensions(path, w, h)) {
        p["w"] = w;
        p["h"] = h;
      } else {
        p["w"] = 0;
        p["h"] = 0;
      }
    }
    jsondata["maxCount"] = PHOTO_MAX_COUNT;
    jsondata["maxBytes"] = PHOTO_MAX_BYTES;
    jsondata["totalBytes"] = (int)photoTotalBytes();
    jsondata["budgetBytes"] = PHOTO_TOTAL_BUDGET;
    String dataout = "";
    serializeJson(jsondata, dataout);
    server.send(200, "text/html", dataout);
  });

  server.on("/uploadPhoto", HTTP_OPTIONS, []() {
    server.send(200);
  });

  server.on("/uploadPhoto", HTTP_POST, []() {
    previousMillis = millis();
    String pincode = NVS.getString("pincode");
    if (pin_UNLOCK != pincode) {
      server.send(200, "text/html", "LOCKED");
      return;
    }
    server.send(200, "text/html", photoUploadError == "" ? "OK" : photoUploadError);
  }, []() {
    HTTPUpload& upload = server.upload();
    String pincode = NVS.getString("pincode");
    if (pin_UNLOCK != pincode) {
      return;
    }
    if (upload.status == UPLOAD_FILE_START) {
      photoUploadError = "";
      photoUploadSize = 0;
      photoUploadCheckedMagic = false;
      photoUploadPath = "";
      String fn = upload.filename;
      fn.toLowerCase();
      if (!fn.endsWith(".jpg") && !fn.endsWith(".jpeg")) {
        photoUploadError = "NOT JPG";
        return;
      }
      int slot = photoNextSlot();
      if (slot < 0 || photoTotalBytes() + PHOTO_MAX_BYTES > PHOTO_TOTAL_BUDGET) {
        photoUploadError = "FULL";
        return;
      }
      photoUploadPath = String(PHOTO_DIR) + "/" + String(slot) + ".jpg";
      photoUploadFile = SPIFFS.open(photoUploadPath, FILE_WRITE);
      if (!photoUploadFile) {
        photoUploadError = "FS ERROR";
        return;
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (!photoUploadFile || photoUploadError != "") {
        return;
      }
      if (!photoUploadCheckedMagic) {
        photoUploadCheckedMagic = true;
        if (upload.currentSize < 2 || upload.buf[0] != 0xFF || upload.buf[1] != 0xD8) {
          photoUploadError = "NOT JPG";
          photoUploadFile.close();
          SPIFFS.remove(photoUploadPath);
          return;
        }
      }
      photoUploadSize += upload.currentSize;
      if (photoUploadSize > PHOTO_MAX_BYTES) {
        photoUploadError = "TOO BIG";
        photoUploadFile.close();
        SPIFFS.remove(photoUploadPath);
        return;
      }
      if (photoUploadFile.write(upload.buf, upload.currentSize) != upload.currentSize) {
        photoUploadError = "FS ERROR";
        photoUploadFile.close();
        SPIFFS.remove(photoUploadPath);
        return;
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (photoUploadFile) {
        photoUploadFile.close();
      }
      if (photoUploadError == "" && photoUploadSize < 1024) {
        photoUploadError = "TOO SMALL";
        SPIFFS.remove(photoUploadPath);
      }
    }
  });

  server.on("/deletePhoto", HTTP_OPTIONS, []() {
    server.send(200);
  });

  server.on("/deletePhoto", HTTP_POST, []() {
    previousMillis = millis();
    String pincode = NVS.getString("pincode");
    if (pin_UNLOCK != pincode) {
      server.send(200, "text/html", "LOCKED");
      return;
    }
    jsondata.clear();
    deserializeJson(jsondata, server.arg(0));
    String name = jsondata["name"].as<String>();
    if (name == "" || name.indexOf('/') >= 0 || name.indexOf("..") >= 0) {
      server.send(200, "text/html", "INVALID");
      return;
    }
    if (SPIFFS.remove(String(PHOTO_DIR) + "/" + name)) {
      server.send(200, "text/html", "OK");
    } else {
      server.send(200, "text/html", "INVALID");
    }
  });

  server.on("/deleteAllPhotos", HTTP_OPTIONS, []() {
    server.send(200);
  });

  server.on("/deleteAllPhotos", HTTP_POST, []() {
    previousMillis = millis();
    String pincode = NVS.getString("pincode");
    if (pin_UNLOCK != pincode) {
      server.send(200, "text/html", "LOCKED");
      return;
    }
    String names[PHOTO_MAX_COUNT];
    int n = photoList(names, PHOTO_MAX_COUNT);
    for (int i = 0; i < n; i++) {
      SPIFFS.remove(String(PHOTO_DIR) + "/" + names[i]);
    }
    server.send(200, "text/html", "OK");
  });

  // -- MEMO API --
  // Memos are one multi-line string in NVS (see memo_cfg.h). The WebUI
  // edits the whole text at once; the device shows it line by line.

  server.on("/getMemos", HTTP_GET, []() {
    previousMillis = millis();
    String pincode = NVS.getString("pincode");
    if (pin_UNLOCK != pincode) {
      server.send(200, "text/html", "LOCKED");
      return;
    }
    jsondata.clear();
    jsondata["memos"] = memoGet();
    jsondata["maxLen"] = MEMO_MAX_LEN;
    String dataout = "";
    serializeJson(jsondata, dataout);
    server.send(200, "text/html", dataout);
  });

  server.on("/setMemos", HTTP_OPTIONS, []() {
    server.send(200);
  });

  server.on("/setMemos", HTTP_POST, []() {
    previousMillis = millis();
    String pincode = NVS.getString("pincode");
    if (pin_UNLOCK != pincode) {
      server.send(200, "text/html", "LOCKED");
      return;
    }
    jsondata.clear();
    deserializeJson(jsondata, server.arg(0));
    if (jsondata["memos"].isNull()) {
      server.send(200, "text/html", "INVALID");
      return;
    }
    String memos = jsondata["memos"].as<String>();
    if (memos.length() > MEMO_MAX_LEN) {
      memos = memos.substring(0, MEMO_MAX_LEN);
    }
    NVS.setString(MEMO_KEY, memos);
    server.send(200, "text/html", "OK");
  });

  // -- UPTIME KUMA API --
  // The status page base URL and slug (see uptime_cfg.h). An empty slug
  // means the app auto-discovers it from /api/entry-page.

  server.on("/getUptimeCfg", HTTP_GET, []() {
    previousMillis = millis();
    String pincode = NVS.getString("pincode");
    if (pin_UNLOCK != pincode) {
      server.send(200, "text/html", "LOCKED");
      return;
    }
    jsondata.clear();
    jsondata["base"] = upGetBase();
    jsondata["slug"] = upGetSlug();
    String dataout = "";
    serializeJson(jsondata, dataout);
    server.send(200, "text/html", dataout);
  });

  server.on("/setUptimeCfg", HTTP_OPTIONS, []() {
    server.send(200);
  });

  server.on("/setUptimeCfg", HTTP_POST, []() {
    previousMillis = millis();
    String pincode = NVS.getString("pincode");
    if (pin_UNLOCK != pincode) {
      server.send(200, "text/html", "LOCKED");
      return;
    }
    jsondata.clear();
    deserializeJson(jsondata, server.arg(0));
    if (jsondata["base"].isNull() || jsondata["slug"].isNull()) {
      server.send(200, "text/html", "INVALID");
      return;
    }
    String base = jsondata["base"].as<String>();
    String slug = jsondata["slug"].as<String>();
    base.trim();
    slug.trim();
    while (base.endsWith("/")) {
      base.remove(base.length() - 1);
    }
    if (base == "" || (!base.startsWith("http://") && !base.startsWith("https://")) ||
        base.length() > 128) {
      server.send(200, "text/html", "INVALID");
      return;
    }
    if (slug.length() > 64) {
      server.send(200, "text/html", "INVALID");
      return;
    }
    for (unsigned int i = 0; i < slug.length(); i++) {
      char c = slug.charAt(i);
      bool okChar = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_';
      if (!okChar) {
        server.send(200, "text/html", "INVALID");
        return;
      }
    }
    // A new base URL invalidates a previously auto-discovered slug,
    // unless the user typed a different slug explicitly.
    if (base != upGetBase() && slug == upGetSlug()) {
      slug = "";
    }
    NVS.setString(UP_KEY_BASE, base);
    NVS.setString(UP_KEY_SLUG, slug);
    server.send(200, "text/html", "OK");
  });


  // -- OTA UPDATE API --
  
  server.on("/update", HTTP_OPTIONS, []() {
    server.send(200);
  });

  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");

    String  pincode = NVS.getString ("pincode");
    if (pin_UNLOCK != pincode) {
      server.send(200, "text/html", "LOCKED");
      return;
    }

    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("Update: %s\n", upload.filename.c_str());
      M5.Lcd.fillScreen(TFT_BLACK);
      // show wait icon
      show_wait_icon();
      M5.Lcd.setFont(&beta8pt7b);
      M5.Lcd.setCursor(10, 53);
      M5.Lcd.print("UPDATING...");

      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
        Update.printError(Serial);
      }

    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }

    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });

  // -- END OTA UPDATE API --
  
  server.begin();
  Serial.println("HTTP server started");
  
  while (!appExitRequested()) {
    // Mask the AP password 5s after it was printed. This runs once and
    // never blocks: the DNS and HTTP handlers below keep running.
    if (Wifi_Mode == "AP" && !wifiPassMasked && wifiPassShownAt != 0 &&
        millis() - wifiPassShownAt > 5000) {
      wifiPassMasked = true;
      int maskLen = Wifi_PASSWORD.length();
      if (current_screen == STICKC) {
        M5.Lcd.setFont(&beta5pt7b);
        M5.Lcd.fillRect(10, 43, maskLen * 5 + 4, 8, bg_color);
        M5.Lcd.setCursor(10, 43);
      } else {
        M5.Lcd.setFont(&beta8pt7b);
        M5.Lcd.fillRect(10, 90, maskLen * 7 + 4, 12, bg_color);
        M5.Lcd.setCursor(10, 90);
      }
      M5.Lcd.setTextColor(txt_color, bg_color);
      for (int i = 0; i < maskLen; i++) {
        M5.Lcd.print("*");
      }
    }
    // Process DNS requests for captive portal
    if (_dnsStarted) {
      dnsServer.processNextRequest();
    }
    server.handleClient();
  }
  
  // Cleanup when leaving screen
  if (_dnsStarted) {
    dnsServer.stop();
    _dnsStarted = false;
  }
  WiFi.mode(WIFI_OFF);
}
