// Saved WiFi networks: up to WIFI_PROFILES_MAX station profiles the
// user can pick between on the device (WiFi screen, BtnB) instead of
// re-typing credentials in the WebUI for every network.
//
// Stored in NVS "wifi_nets" as a JSON array: [{"s":ssid,"p":pass}].
// NVS "wifi_last" holds the SSID that connected most recently; it is
// tried first on every automatic connect. The legacy single network
// (NVS "wifi_ssid" / "wifi_password") is migrated into the list on
// first use and kept in sync with the last-used profile, so existing
// readers of the legacy keys keep working unchanged.

#ifndef WIFI_PROFILES_H
#define WIFI_PROFILES_H

#include <WiFi.h>
#include <ArduinoJson.h>

#define WIFI_PROFILES_MAX 8

struct WifiProfile {
  String ssid;
  String pass;
};

// One-time migration: seed the list from the legacy single network.
void wifiProfilesMigrate() {
  if (NVS.getString("wifi_nets") != "") {
    return;
  }
  String ssid = NVS.getString("wifi_ssid");
  if (ssid == "") {
    return;
  }
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  JsonObject first = arr.add<JsonObject>();
  first["s"] = ssid;
  first["p"] = NVS.getString("wifi_password");
  String out;
  serializeJson(doc, out);
  NVS.setString("wifi_nets", out);
  NVS.setString("wifi_last", ssid);
}

// Load the saved profiles into out[]. Returns the count
// (0..WIFI_PROFILES_MAX).
int wifiProfilesLoad(WifiProfile out[], int maxCount) {
  wifiProfilesMigrate();
  String raw = NVS.getString("wifi_nets");
  if (raw == "") {
    return 0;
  }
  JsonDocument doc;
  if (deserializeJson(doc, raw) != DeserializationError::Ok) {
    return 0;
  }
  int n = 0;
  for (JsonObject item : doc.as<JsonArray>()) {
    if (n >= maxCount) {
      break;
    }
    String s = item["s"] | "";
    if (s == "") {
      continue;
    }
    out[n].ssid = s;
    out[n].pass = item["p"] | "";
    n++;
  }
  return n;
}

int wifiProfilesCount() {
  WifiProfile list[WIFI_PROFILES_MAX];
  return wifiProfilesLoad(list, WIFI_PROFILES_MAX);
}

void wifiProfilesStore(WifiProfile list[], int count) {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < count; i++) {
    JsonObject item = arr.add<JsonObject>();
    item["s"] = list[i].ssid;
    item["p"] = list[i].pass;
  }
  String out;
  serializeJson(doc, out);
  NVS.setString("wifi_nets", out);
}

// Add a network, or update the password of an existing one. Returns
// false when the SSID is empty, or the list is full and the SSID is
// new.
bool wifiProfileUpsert(const String& ssid, const String& pass) {
  if (ssid == "") {
    return false;
  }
  WifiProfile list[WIFI_PROFILES_MAX];
  int n = wifiProfilesLoad(list, WIFI_PROFILES_MAX);
  for (int i = 0; i < n; i++) {
    if (list[i].ssid == ssid) {
      list[i].pass = pass;
      wifiProfilesStore(list, n);
      return true;
    }
  }
  if (n >= WIFI_PROFILES_MAX) {
    return false;
  }
  list[n].ssid = ssid;
  list[n].pass = pass;
  wifiProfilesStore(list, n + 1);
  return true;
}

bool wifiProfileDelete(const String& ssid) {
  WifiProfile list[WIFI_PROFILES_MAX];
  int n = wifiProfilesLoad(list, WIFI_PROFILES_MAX);
  int w = 0;
  bool found = false;
  for (int i = 0; i < n; i++) {
    if (list[i].ssid == ssid) {
      found = true;
      continue;
    }
    list[w++] = list[i];
  }
  if (!found) {
    return false;
  }
  wifiProfilesStore(list, w);
  if (NVS.getString("wifi_last") == ssid) {
    NVS.setString("wifi_last", "");
  }
  return true;
}

// Mark a profile as the one in use: it becomes the auto-connect
// favourite, and the legacy single-network keys follow it, so the
// on-demand connect helper (net.h) and the info the WiFi screen
// prints stay in sync. Also updates the runtime globals.
void wifiProfileMarkUsed(const WifiProfile& profile) {
  NVS.setString("wifi_last", profile.ssid);
  NVS.setString("wifi_ssid", profile.ssid);
  NVS.setString("wifi_password", profile.pass);
  Wifi_SSID = profile.ssid;
  Wifi_PASSWORD = profile.pass;
  Wifi_Mode = "STA";
}

// Connect to one specific profile, waiting up to timeoutMs. On
// success the profile is marked used. The caller owns any AP-mode
// teardown (captive portal DNS) around this.
bool wifiConnectProfile(const WifiProfile& profile, unsigned long timeoutMs) {
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.begin(profile.ssid.c_str(), profile.pass.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > timeoutMs) {
      break;
    }
    delay(250);
  }
  if (WiFi.status() == WL_CONNECTED) {
    wifiProfileMarkUsed(profile);
    return true;
  }
  return false;
}

// Connect to the best saved network: the last-used one first, then
// the rest in saved order. Bounded by perProfileMs per network and
// totalMs overall. On success the globals and last-used are updated
// (wifiProfileMarkUsed) and true is returned.
bool wifiConnectSaved(unsigned long perProfileMs, unsigned long totalMs) {
  WifiProfile list[WIFI_PROFILES_MAX];
  int n = wifiProfilesLoad(list, WIFI_PROFILES_MAX);
  if (n == 0) {
    return false;
  }
  String last = NVS.getString("wifi_last");
  unsigned long startAll = millis();
  for (int sweep = 0; sweep < 2; sweep++) {
    for (int i = 0; i < n; i++) {
      bool isLast = (list[i].ssid == last);
      if ((sweep == 0) != isLast) {
        continue;  // first sweep tries only the last-used network
      }
      if (millis() - startAll > totalMs) {
        return false;
      }
      if (wifiConnectProfile(list[i], perProfileMs)) {
        return true;
      }
    }
  }
  return false;
}

#endif
