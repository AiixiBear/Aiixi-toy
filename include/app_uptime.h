// Uptime Kuma status: shows each monitor of the user's public status
// page (https://uptime.aiixi.cc) with its latest up/down state and 24h
// uptime, fetched over HTTPS and cached for 5 minutes.
//
// Public status page API (Uptime Kuma, no key needed):
//   GET /api/entry-page
//     -> {"type":"statusPageMatchedDomain","statusPageSlug":"<slug>"}
//     or {"type":"entryPage","entryPage":"statusPage-<slug>"}
//   GET /api/status-page/<slug>
//     -> {publicGroupList: [{name, monitorList: [{id, name, type}]}]}
//   GET /api/status-page/heartbeat/<slug>
//     -> {heartbeatList: {"<id>": [{status, time, msg, ping}, ...]},
//         uptimeList: {"<id>_24": 0.9998, ...}}
//     status: 0 DOWN, 1 UP, 2 PENDING, 3 MAINTENANCE. Up to 100 beats
//     per monitor, newest last.
//
// The heartbeat body can be tens of kilobytes, so it is never loaded
// into a String or a JsonDocument. Instead uptimeScanHeartbeat() reads
// the response stream in small chunks and keeps only the LAST status
// and ping of each known monitor plus the 24h uptime, using a few
// hundred bytes of state. It assumes the server sends Content-Length
// (Express res.json does); without one it reads until the stream stalls.

#ifndef APP_UPTIME_H
#define APP_UPTIME_H

#include <ArduinoJson.h>
#include "uptime_cfg.h"

#define UPTIME_MAX_MONITORS 12
#define UPTIME_CACHE_MS (5UL * 60UL * 1000UL)

struct UptimeMonitor {
  int id;
  String name;
  int status;     // -1 unknown, 0 DOWN, 1 UP, 2 PENDING, 3 MAINTENANCE
  int ping;       // ms, -1 unknown
  float up24;     // 0..1, -1 unknown
};

static UptimeMonitor uptimeMonitors[UPTIME_MAX_MONITORS];
static int uptimeMonitorCount = 0;
static unsigned long uptimeLastFetch = 0;
static String uptimeError = "";

static int uptimeFindMonitor(int id) {
  for (int i = 0; i < uptimeMonitorCount; i++) {
    if (uptimeMonitors[i].id == id) {
      return i;
    }
  }
  return -1;
}

static void uptimeNote(const String& msg) {
  M5.Lcd.fillRect(0, toolbar_height, screen_x, screen_y - toolbar_height, bg_color);
  M5.Lcd.setFont(&beta8pt7b);
  M5.Lcd.setTextColor(txt_color, bg_color);
  M5.Lcd.setCursor(10, (current_screen == STICKC) ? 45 : 77);
  M5.Lcd.print(msg);
}

// Discover the status page slug for this base URL. Returns "" on
// failure. A discovered slug is saved to NVS so later runs skip this.
static String uptimeDiscoverSlug(const String& base) {
  String body;
  if (!netHttpsGet(base + "/api/entry-page", body)) {
    return "";
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    return "";
  }
  const char* type = doc["type"];
  if (type == nullptr) {
    return "";
  }
  if (String(type) == "statusPageMatchedDomain") {
    const char* slug = doc["statusPageSlug"];
    return slug == nullptr ? "" : String(slug);
  }
  if (String(type) == "entryPage") {
    String ep = doc["entryPage"].as<String>();
    if (ep.startsWith("statusPage-")) {
      return ep.substring(11);
    }
  }
  return "";
}

// Fetch the monitor list from /api/status-page/<slug>. Keeps the id and
// name of at most UPTIME_MAX_MONITORS monitors, in page order.
static bool uptimeFetchMonitors(const String& base, const String& slug) {
  String body;
  if (!netHttpsGet(base + "/api/status-page/" + slug, body)) {
    return false;
  }
  JsonDocument filter;
  filter["publicGroupList"][0]["name"] = true;
  filter["publicGroupList"][0]["monitorList"][0]["id"] = true;
  filter["publicGroupList"][0]["monitorList"][0]["name"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
    return false;
  }
  JsonArray groups = doc["publicGroupList"].as<JsonArray>();
  uptimeMonitorCount = 0;
  for (JsonObject group : groups) {
    JsonArray monitors = group["monitorList"].as<JsonArray>();
    for (JsonObject m : monitors) {
      if (uptimeMonitorCount >= UPTIME_MAX_MONITORS) {
        return true;
      }
      UptimeMonitor& mon = uptimeMonitors[uptimeMonitorCount++];
      mon.id = m["id"].as<int>();
      mon.name = m["name"].as<String>();
      mon.status = -1;
      mon.ping = -1;
      mon.up24 = -1;
    }
  }
  return true;
}

// Feed one byte of the heartbeat JSON into the scanner state. The
// scanner tokenizes strings, numbers and punctuation on the fly; it
// only reacts to values whose key is "status" or "ping" inside a known
// monitor's heartbeat array, or "<id>_24" inside uptimeList.
struct UptimeScanner {
  int section = 0;        // 0 none, 1 heartbeatList, 2 uptimeList
  int depth = 0;          // brace depth
  int hbObjDepth = 0;     // brace depth of the heartbeatList object
  int upObjDepth = 0;     // brace depth of the uptimeList object
  int arrDepth = 0;       // bracket depth
  int curMon = -1;        // monitor id of the array being scanned
  int monArrDepth = 0;
  String pending;         // string being captured, or last completed one
  String key;             // key of the value being read
  String token;           // number / literal token being built
  bool inString = false;
  bool escaped = false;
  bool expectValue = false;

  static bool isNumberChar(char c) {
    return (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' ||
           c == 'e' || c == 'E';
  }

  static bool isWordChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
  }

  static bool isDigits(const String& s) {
    if (s.length() == 0) {
      return false;
    }
    for (unsigned int i = 0; i < s.length(); i++) {
      if (s.charAt(i) < '0' || s.charAt(i) > '9') {
        return false;
      }
    }
    return true;
  }

  void finishToken() {
    if (token.length() == 0 || !expectValue) {
      token = "";
      return;
    }
    expectValue = false;
    if (curMon >= 0) {
      int idx = uptimeFindMonitor(curMon);
      if (idx >= 0) {
        if (key == "status") {
          uptimeMonitors[idx].status = token.toInt();
        } else if (key == "ping") {
          uptimeMonitors[idx].ping = (token == "null") ? -1 : token.toInt();
        }
      }
    } else if (section == 2 && key.endsWith("_24")) {
      int id = key.substring(0, key.length() - 3).toInt();
      int idx = uptimeFindMonitor(id);
      if (idx >= 0) {
        uptimeMonitors[idx].up24 = token.toFloat();
      }
    }
    token = "";
  }

  void feed(char c) {
    if (inString) {
      if (escaped) {
        pending += c;
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        inString = false;
      } else {
        pending += c;
      }
      return;
    }
    if (c == '"') {
      inString = true;
      pending = "";
      return;
    }
    if (c == '{') {
      finishToken();
      depth++;
      if (expectValue && key == "heartbeatList") {
        section = 1;
        hbObjDepth = depth;
      } else if (expectValue && key == "uptimeList") {
        section = 2;
        upObjDepth = depth;
      }
      expectValue = false;
      return;
    }
    if (c == '}') {
      finishToken();
      if (section == 1 && depth == hbObjDepth) {
        section = 0;
      } else if (section == 2 && depth == upObjDepth) {
        section = 0;
      }
      depth--;
      expectValue = false;
      return;
    }
    if (c == '[') {
      finishToken();
      arrDepth++;
      if (section == 1 && depth == hbObjDepth && isDigits(key)) {
        curMon = key.toInt();
        monArrDepth = arrDepth;
      }
      expectValue = false;
      return;
    }
    if (c == ']') {
      finishToken();
      if (curMon >= 0 && arrDepth == monArrDepth) {
        curMon = -1;
      }
      arrDepth--;
      expectValue = false;
      return;
    }
    if (c == ':') {
      finishToken();
      key = pending;
      expectValue = true;
      return;
    }
    if (c == ',') {
      finishToken();
      expectValue = false;
      return;
    }
    if (expectValue && (isNumberChar(c) || isWordChar(c))) {
      token += c;
      return;
    }
    // Whitespace and anything else: end of a token.
    finishToken();
  }
};

// Stream the heartbeat body through the scanner. Reads exactly
// contentLength bytes when known, otherwise until the stream stalls
// for 10 seconds. Returns false on a stalled stream.
static bool uptimeScanHeartbeat(WiFiClient* stream, int contentLength) {
  UptimeScanner sc;
  uint8_t buf[256];
  long remaining = contentLength;
  unsigned long lastData = millis();
  while (true) {
    if (contentLength >= 0 && remaining <= 0) {
      return true;
    }
    int avail = stream->available();
    if (avail > 0) {
      int want = avail < (int)sizeof(buf) ? avail : (int)sizeof(buf);
      if (contentLength >= 0 && want > remaining) {
        want = (int)remaining;
      }
      int n = stream->readBytes(buf, want);
      if (n <= 0) {
        return false;
      }
      lastData = millis();
      if (contentLength >= 0) {
        remaining -= n;
      }
      for (int i = 0; i < n; i++) {
        sc.feed((char)buf[i]);
      }
    } else {
      if (millis() - lastData > 10000) {
        return contentLength < 0;  // unknown length: stall means done
      }
      delay(5);
    }
  }
}

// Full refresh: WiFi, slug discovery if needed, monitor list, then the
// heartbeat stream. On success the cache timestamp is updated.
static bool uptimeFetch() {
  String base = upGetBase();
  String slug = upGetSlug();
  uptimeError = "";

  uptimeNote("WIFI...");
  if (!netStaConnect()) {
    uptimeError = "NO WIFI SETUP";
    return false;
  }

  if (slug == "") {
    uptimeNote("FINDING PAGE...");
    slug = uptimeDiscoverSlug(base);
    if (slug == "") {
      netDisconnect();
      uptimeError = "NO SLUG - SET IN WEBUI";
      return false;
    }
    NVS.setString(UP_KEY_SLUG, slug);
  }

  uptimeNote("MONITORS...");
  if (!uptimeFetchMonitors(base, slug)) {
    netDisconnect();
    uptimeError = "STATUS PAGE FAILED";
    return false;
  }
  if (uptimeMonitorCount == 0) {
    netDisconnect();
    uptimeError = "NO MONITORS";
    return false;
  }

  uptimeNote("HEARTBEATS...");
  WiFiClientSecure client;
  HTTPClient http;
  int code = netHttpsGetStream(base + "/api/status-page/heartbeat/" + slug, http, client);
  if (code != HTTP_CODE_OK) {
    http.end();
    netDisconnect();
    uptimeError = "HEARTBEAT FAILED";
    return false;
  }
  bool ok = uptimeScanHeartbeat(http.getStreamPtr(), http.getSize());
  http.end();
  netDisconnect();
  if (!ok) {
    uptimeError = "HEARTBEAT READ FAILED";
    return false;
  }
  uptimeLastFetch = millis();
  return true;
}

static String uptimeStatusText(int status) {
  switch (status) {
    case 0: return "DOWN";
    case 1: return "UP";
    case 2: return "PENDING";
    case 3: return "MAINT";
    default: return "??";
  }
}

void uptime_screen() {
  const int rowH = (current_screen == STICKC) ? 31 : 38;
  const int visibleRows = (screen_y - toolbar_height) / rowH;
  int sel = 0;
  int winStart = 0;
  bool needRepaint = true;
  bool fetched = false;

  while (!appExitRequested()) {
    if (!fetched) {
      fetched = true;
      if (millis() - uptimeLastFetch > UPTIME_CACHE_MS || uptimeMonitorCount == 0) {
        uptimeFetch();
        sel = 0;
        winStart = 0;
      }
      needRepaint = true;
    }

    if (M5.BtnA.wasClicked()) {
      previousMillis = millis();
      if (uptimeMonitorCount > 0) {
        sel = (sel + 1) % uptimeMonitorCount;
        if (sel < winStart) {
          winStart = sel;
        }
        if (sel >= winStart + visibleRows) {
          winStart = sel - visibleRows + 1;
        }
      }
      needRepaint = true;
    }

    if (M5.BtnA.wasHold()) {
      previousMillis = millis();
      uptimeFetch();
      sel = 0;
      winStart = 0;
      needRepaint = true;
    }

    if (needRepaint) {
      needRepaint = false;
      M5.Lcd.fillRect(0, toolbar_height, screen_x, screen_y - toolbar_height, bg_color);
      M5.Lcd.setTextColor(txt_color, bg_color);
      if (uptimeError != "") {
        M5.Lcd.setFont(&beta8pt7b);
        M5.Lcd.setCursor(10, (current_screen == STICKC) ? 45 : 77);
        M5.Lcd.print(uptimeError);
        M5.Lcd.setFont(&beta5pt7b);
        M5.Lcd.setCursor(10, (current_screen == STICKC) ? 65 : 97);
        M5.Lcd.print("HOLD BTNA TO RETRY");
      } else {
        for (int r = 0; r < visibleRows; r++) {
          int idx = winStart + r;
          if (idx >= uptimeMonitorCount) {
            break;
          }
          int rowTop = toolbar_height + r * rowH;
          bool selected = (idx == sel);
          if (selected) {
            M5.Lcd.fillRect(0, rowTop, screen_x, rowH, txt_color);
          }
          UptimeMonitor& mon = uptimeMonitors[idx];
          String right = uptimeStatusText(mon.status);
          if (mon.up24 >= 0) {
            right += " " + String(mon.up24 * 100, 2) + "%";
          }
          M5.Lcd.setFont(&beta8pt7b);
          M5.Lcd.setTextColor(selected ? bg_color : txt_color, selected ? txt_color : bg_color);
          int rightW = M5.Lcd.textWidth(right);
          M5.Lcd.setCursor(screen_x - 5 - rightW, rowTop + rowH / 2 - 5);
          M5.Lcd.print(right);
          // Truncate the name so it does not run into the status text.
          String name = mon.name;
          while (name.length() > 0 &&
                 M5.Lcd.textWidth(name) > screen_x - rightW - 20) {
            name.remove(name.length() - 1);
          }
          M5.Lcd.setCursor(8, rowTop + rowH / 2 - 5);
          M5.Lcd.print(name);
        }
      }
    }

    delay(10);
  }
}

#endif
