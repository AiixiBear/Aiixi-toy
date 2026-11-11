// App launcher: every feature on the device is an App.
//
// The home screen lists the registered apps. BtnB moves the selection,
// BtnA enters the selected app. Inside an app, BtnB held returns to the
// home screen; BtnB short press is unused inside apps. The old behaviour
// (BtnB short press cycles screens) is gone: the home list replaces it.
//
// Each app is a blocking run() function that returns when the user
// leaves it, following the same pattern as the original OTP screens.
// Shared services (time, WiFi, NVS settings) stay in their existing
// files; this file only owns the registry and the home screen.

#ifndef APPS_H
#define APPS_H

struct AppDef {
  const char* id;     // stable id, used for the NVS enable flag
  const char* name;   // short name shown in the toolbar
  const char* label;  // name shown in the home list
  void (*run)();      // blocking screen function
};

// Registry, in home-list order. New apps are appended here.
static const AppDef apps[] = {
  {"totp",    "TOTP",     "TOTP",       OTP_screen},
  {"otplist", "OTP List", "OTP List",   OTP_list_screen},
  {"time",    "Time",     "Time",       Time_screen},
  {"weather", "Weather",  "Weather",    weather_screen},
  {"air",     "Air",      "Air Quality", air_screen},
  {"media",   "Media",    "Media Remote", media_screen},
  {"uptime",  "Uptime",   "Uptime Kuma",  uptime_screen},
  {"level",   "Level",    "Level",        level_screen},
  {"db",      "dB",       "Decibel Meter", db_screen},
  {"qr",      "QR",       "QR Card",      qr_screen},
  {"photo",   "Photos",   "Photo Frame",  photo_screen},
  {"memo",    "Memos",    "Memo Quick View", memo_screen},
  {"wifi",    "WiFi",     "WiFi Setup", Wifi_screen},
  {"drift",   "Drift",    "Fix Drift",  Time_drift_screen},
};
static const int appCount = sizeof(apps) / sizeof(apps[0]);

// An app is enabled unless its NVS flag says otherwise. Defaults are on,
// so a fresh device shows every app; the WebUI writes the flags.
bool appIsEnabled(const AppDef& app) {
  return NVS.getInt(String("app_en_") + app.id, 1) == 1;
}

// App display order, stored in NVS as a comma-separated list of app ids
// under the key "app_order". No key (or an empty value) means registry
// order. Unknown ids are ignored; apps missing from the stored list are
// appended at the end in registry order, so a newly added app always
// shows up. Returns the number of indices written to out[].
int loadAppOrder(int* out, int maxOut) {
  int count = 0;
  String order = NVS.getString("app_order");
  int start = 0;
  while (start < (int)order.length() && count < maxOut) {
    int comma = order.indexOf(',', start);
    String id = (comma < 0) ? order.substring(start) : order.substring(start, comma);
    id.trim();
    for (int i = 0; i < appCount; i++) {
      if (id == apps[i].id) {
        bool dup = false;
        for (int j = 0; j < count; j++) {
          if (out[j] == i) { dup = true; break; }
        }
        if (!dup) out[count++] = i;
        break;
      }
    }
    if (comma < 0) break;
    start = comma + 1;
  }
  for (int i = 0; i < appCount; i++) {
    bool present = false;
    for (int j = 0; j < count; j++) {
      if (out[j] == i) { present = true; break; }
    }
    if (!present && count < maxOut) out[count++] = i;
  }
  return count;
}

// WebUI endpoints for enabling and disabling apps. Registered from
// setup() because this file is included after screen3.h, where the
// other endpoints live. The WiFi Setup app cannot be disabled: it is
// the only way back to the WebUI, so disabling it would strand the
// device with no way to re-enable anything.
void registerAppEndpoints() {
  server.on("/getApps", HTTP_GET, []() {
    previousMillis = millis();
    String pincode = NVS.getString("pincode");
    if (pin_UNLOCK != pincode) {
      server.send(200, "text/html", "LOCKED");
      return;
    }
    jsondata.clear();
    JsonArray list = jsondata["apps"].to<JsonArray>();
    int ordered[appCount];
    int orderedCount = loadAppOrder(ordered, appCount);
    for (int k = 0; k < orderedCount; k++) {
      int i = ordered[k];
      JsonObject a = list.add<JsonObject>();
      a["id"] = apps[i].id;
      a["label"] = apps[i].label;
      a["enabled"] = appIsEnabled(apps[i]);
    }
    String dataout = "";
    serializeJson(jsondata, dataout);
    server.send(200, "text/html", dataout);
  });

  server.on("/setApps", HTTP_OPTIONS, []() {
    server.send(200);
  });

  server.on("/setApps", HTTP_POST, []() {
    previousMillis = millis();
    String pincode = NVS.getString("pincode");
    if (pin_UNLOCK != pincode) {
      server.send(200, "text/html", "LOCKED");
      return;
    }
    jsondata.clear();
    deserializeJson(jsondata, server.arg(0));
    if (jsondata["id"].isNull() || jsondata["enabled"].isNull()) {
      server.send(200, "text/html", "INVALID");
      return;
    }
    String id = jsondata["id"].as<String>();
    bool enabled = jsondata["enabled"].as<bool>();
    if (id == "wifi" && !enabled) {
      server.send(200, "text/html", "INVALID");
      return;
    }
    bool found = false;
    for (int i = 0; i < appCount; i++) {
      if (id == apps[i].id) {
        found = true;
        break;
      }
    }
    if (!found) {
      server.send(200, "text/html", "INVALID");
      return;
    }
    NVS.setInt(String("app_en_") + id, enabled ? 1 : 0);
    server.send(200, "text/html", "OK");
  });

  // Reorder the home-screen app list. Body: {"order": ["id", ...]}.
  // Every id must be a registered app; duplicates are dropped. Ids not
  // listed keep registry order after the listed ones (loadAppOrder
  // appends them), so a partial list is valid and a new app is never
  // lost. An empty list resets to registry order.
  server.on("/setAppOrder", HTTP_OPTIONS, []() {
    server.send(200);
  });

  server.on("/setAppOrder", HTTP_POST, []() {
    previousMillis = millis();
    String pincode = NVS.getString("pincode");
    if (pin_UNLOCK != pincode) {
      server.send(200, "text/html", "LOCKED");
      return;
    }
    jsondata.clear();
    deserializeJson(jsondata, server.arg(0));
    if (jsondata["order"].isNull() || !jsondata["order"].is<JsonArray>()) {
      server.send(200, "text/html", "INVALID");
      return;
    }
    bool seen[appCount] = {};
    String stored = "";
    JsonArray order = jsondata["order"].as<JsonArray>();
    for (JsonVariant v : order) {
      if (v.isNull()) {
        server.send(200, "text/html", "INVALID");
        return;
      }
      String id = v.as<String>();
      int idx = -1;
      for (int i = 0; i < appCount; i++) {
        if (id == apps[i].id) { idx = i; break; }
      }
      if (idx < 0) {
        server.send(200, "text/html", "INVALID");
        return;
      }
      if (seen[idx]) continue;
      seen[idx] = true;
      if (stored.length() > 0) stored += ",";
      stored += id;
    }
    NVS.setString("app_order", stored);
    server.send(200, "text/html", "OK");
  });

  // Bluetooth on/off. GET returns {"enabled": true/false}; POST takes
  // {"enabled": bool} and applies it immediately, no reboot needed.
  // The setting is stored in NVS under "ble_enabled" (default on).
  server.on("/getBle", HTTP_GET, []() {
    previousMillis = millis();
    String pincode = NVS.getString("pincode");
    if (pin_UNLOCK != pincode) {
      server.send(200, "text/html", "LOCKED");
      return;
    }
    jsondata.clear();
    jsondata["enabled"] = bleEnabled();
    String dataout = "";
    serializeJson(jsondata, dataout);
    server.send(200, "text/html", dataout);
  });

  server.on("/setBle", HTTP_OPTIONS, []() {
    server.send(200);
  });

  server.on("/setBle", HTTP_POST, []() {
    previousMillis = millis();
    String pincode = NVS.getString("pincode");
    if (pin_UNLOCK != pincode) {
      server.send(200, "text/html", "LOCKED");
      return;
    }
    jsondata.clear();
    deserializeJson(jsondata, server.arg(0));
    if (jsondata["enabled"].isNull()) {
      server.send(200, "text/html", "INVALID");
      return;
    }
    bleHidSetEnabled(jsondata["enabled"].as<bool>());
    server.send(200, "text/html", "OK");
  });
}

// Home screen: the app list. Never returns; apps are entered by calling
// their run() function and return here when the user leaves them.
void home_screen() {
  int ordered[appCount];
  int orderedCount = loadAppOrder(ordered, appCount);
  int ids[appCount];
  int count = 0;
  for (int i = 0; i < orderedCount; i++) {
    if (appIsEnabled(apps[ordered[i]])) {
      ids[count++] = ordered[i];
    }
  }

  // Same row geometry as the OTP list screen: two text lines need
  // breathing room, so 3 rows of 38px on the Plus2 and 2 rows of 31px
  // on the M5StickC. The window scrolls so the selection stays visible.
  const int rowH = (current_screen == STICKC) ? 31 : 38;
  const int visibleRows = (screen_y - toolbar_height) / rowH;
  int sel = 0;
  int winStart = 0;
  bool needRepaint = true;

  currentAppName = "Home";

  while (true) {
    unsigned long nowMs = millis();
    if (nowMs - previousMillis >= timeout_ScreenOn) {
      M5.Power.powerOff();
    }
    M5.update();
    drawAppToolbar();

    if (count == 0) {
      if (needRepaint) {
        needRepaint = false;
        M5.Lcd.fillRect(0, toolbar_height, screen_x, screen_y - toolbar_height, bg_color);
        M5.Lcd.setFont(&beta8pt7b);
        M5.Lcd.setTextColor(txt_color, bg_color);
        M5.Lcd.setCursor(10, 40);
        M5.Lcd.print("NO APPS");
        M5.Lcd.setFont(&beta5pt7b);
        M5.Lcd.setCursor(10, 55);
        M5.Lcd.print("ENABLE SOME IN WEBUI");
      }
      delay(50);
      continue;
    }

    // BtnB moves the selection down, wrapping to the first entry.
    if (M5.BtnB.wasPressed()) {
      previousMillis = nowMs;
      sel = (sel + 1) % count;
      if (sel < winStart) {
        winStart = sel;
      }
      if (sel >= winStart + visibleRows) {
        winStart = sel - visibleRows + 1;
      }
      needRepaint = true;
    }

    // BtnA enters the selected app. When the app returns (BtnB held
    // inside it), the list is repainted.
    if (M5.BtnA.wasClicked()) {
      previousMillis = nowMs;
      const AppDef& app = apps[ids[sel]];
      currentAppName = app.name;
      M5.Lcd.fillRect(0, toolbar_height, screen_x, screen_y - toolbar_height, bg_color);
      app.run();
      currentAppName = "Home";
      needRepaint = true;
    }

    if (needRepaint) {
      needRepaint = false;
      M5.Lcd.fillRect(0, toolbar_height, screen_x, screen_y - toolbar_height, bg_color);
      for (int r = 0; r < visibleRows; r++) {
        int idx = winStart + r;
        if (idx >= count) {
          break;
        }
        int rowTop = toolbar_height + r * rowH;
        const AppDef& app = apps[ids[idx]];
        bool selected = (idx == sel);
        if (selected) {
          M5.Lcd.fillRect(0, rowTop, screen_x, rowH, txt_color);
        }
        M5.Lcd.setTextColor(selected ? bg_color : txt_color, selected ? txt_color : bg_color);
        M5.Lcd.setFont(&beta8pt7b);
        M5.Lcd.setCursor(8, rowTop + rowH / 2 - 5);
        M5.Lcd.print(app.label);
        if (selected) {
          String pos = String(idx + 1) + "/" + String(count);
          M5.Lcd.setCursor(screen_x - 5 - M5.Lcd.textWidth(pos), rowTop + rowH / 2 - 5);
          M5.Lcd.print(pos);
        }
      }
    }

    delay(10);
  }
}

#endif
