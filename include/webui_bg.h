// Background WebUI: keep the WiFi radio and the HTTP server running
// while the user is in other apps, so the WebUI stays reachable without
// sitting on the WiFi screen.
//
// The switch is NVS "webui_bg" (default 0, off). Both modes work: STA
// stays connected to the saved network (mDNS aiixi-toy.local), AP keeps
// broadcasting Aiixi-toy_<MAC last 6> with the captive portal DNS.
// Keeping the radio on costs battery, so the switch is off by default
// and can be toggled from the WebUI Settings or on the device, in the
// WiFi screen with button A.
//
// This header is included from main.cpp before screen.h, so the shared
// per-loop tick appExitRequested() can service the server, and before
// net.h, so the on-demand network helpers do not tear the radio down
// while the background service is active. It needs the globals from
// variable_runtime.h and the objects declared in main.cpp.

#ifndef WEBUI_BG_H
#define WEBUI_BG_H

#include <WiFi.h>
#include <DNSServer.h>

// Captive portal DNS server, owned by the background service and the
// WiFi screen together: whichever one brings WiFi up in AP mode starts
// it, and the background tick services it.
DNSServer dnsServer;
const byte DNS_PORT = 53;
bool _dnsStarted = false;

// True while the WiFi screen is running its own loop. The screen owns
// WiFi then: the background service only adopts the running network
// instead of bringing WiFi up a second time, and turning the
// background off leaves WiFi up until the screen exits.
bool _webuiBgForeground = false;

// True while the background service is running (server begun, WiFi up
// for background use). Distinct from the NVS setting: the setting says
// whether the service should run, this says whether it is running.
bool _webuiBgActive = false;

bool syncTimeFromNTP();    // defined in screen3.h
void registerWebRoutes();  // defined in screen3.h

bool webuiBgEnabled() {
  return NVS.getInt("webui_bg", 0) == 1;
}

bool webuiBgActive() {
  return _webuiBgActive;
}

// Bring WiFi up in the mode stored in NVS, with the same rules as the
// WiFi screen: AP broadcasts Aiixi-toy_<MAC last 6> (a fresh 12-char
// random password unless a fixed password is configured), STA connects
// to the saved network and falls back to AP after 10 s without a link.
// Sets the Wifi_Mode / Wifi_SSID / Wifi_PASSWORD globals the WiFi
// screen prints. Returns the effective mode ("AP" or "STA").
String webuiWifiStart() {
  String mode = NVS.getString("wifiMode");
  if (mode == "") {
    mode = "AP";
  }

  // AP network name, from the MAC address. Built exactly like the
  // WiFi screen builds it, including the 16-byte buffer the screen
  // uses, so the broadcast name is identical either way.
  String macaddr = String(WiFi.macAddress());
  macaddr.replace(":", "");
  macaddr = macaddr.substring(6);
  String apSsid = "Aiixi-toy_" + macaddr;
  char AP_ssid[16];
  apSsid.toCharArray(AP_ssid, 16);

  // AP password: a fresh 12-char random password unless a fixed
  // password is configured. The STA password is always the stored
  // one; an empty stored password falls back to the documented
  // default, same as the WiFi screen.
  String passStatic = NVS.getString("wifi_password");
  if (passStatic == "") {
    passStatic = "1A2B3C4D5E";
  }
  String passGen = "";
  for (int i = 0; i < 12; i++) {
    passGen += rondom_letters[random(0, rondom_letters.length())];
  }

  if (mode == "AP") {
    Wifi_SSID = apSsid;
    Wifi_PASSWORD = (NVS.getInt("passwordMode") == 0) ? passGen : passStatic;
  } else {
    Wifi_SSID = NVS.getString("wifi_ssid");
    Wifi_PASSWORD = passStatic;
  }
  Wifi_Mode = mode;

  if (mode == "AP") {
    Serial.println("Starting in AP mode");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_ssid, Wifi_PASSWORD.c_str());
    if (!_dnsStarted) {
      dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
      _dnsStarted = true;
      Serial.println("Captive portal DNS started");
    }
  } else {
    Serial.println("Starting in STA mode");
    // Saved networks first (the last-used one wins); the legacy
    // single network is only used when no profiles are stored.
    bool connected = wifiConnectSaved(8000, 24000);
    if (!connected && wifiProfilesCount() == 0 && Wifi_SSID != "") {
      WiFi.mode(WIFI_STA);
      WiFi.begin(Wifi_SSID.c_str(), Wifi_PASSWORD.c_str());
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
    }
    Serial.println("Connection status: " + String(WiFi.status()));
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Connected to " + Wifi_SSID + " with IP: " + WiFi.localIP().toString());
    }
  }

  // STA without a link falls back to AP, same as the WiFi screen.
  if (mode != "AP" && WiFi.status() != WL_CONNECTED) {
    Serial.println("Failed to connect to the specified network. Falling back to AP mode.");
    Wifi_SSID = apSsid;
    Wifi_Mode = "AP";
    Wifi_PASSWORD = passGen;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_ssid, passGen.c_str());
    Serial.println("AP mode started (fallback)");
    if (!_dnsStarted) {
      dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
      _dnsStarted = true;
      Serial.println("Captive portal DNS started (fallback)");
    }
  }

  // STA mode with a working uplink: correct the RTC from NTP. AP mode
  // has no uplink, and a failed sync simply keeps the previous time.
  if (Wifi_Mode != "AP" && WiFi.status() == WL_CONNECTED) {
    syncTimeFromNTP();
  }

  // use mdns to resolve the hostname
  if (MDNS.begin("aiixi-toy")) {  // http://aiixi-toy.local
    Serial.println("mDNS responder started");
    _mdnsStarted = true;
  } else {
    Serial.println("Error setting up MDNS responder!");
    _mdnsStarted = false;
  }

  return Wifi_Mode;
}

// Start the background service: WiFi up (unless the WiFi screen
// already has it up) plus the HTTP server. Safe to call when it is
// already running. Returns true when the service is up.
bool webuiBgStart() {
  if (_webuiBgActive) {
    return true;
  }
  registerWebRoutes();
  if (!_webuiBgForeground) {
    webuiWifiStart();
  }
  // WebServer::begin() closes and re-binds first, so calling it while
  // the foreground WiFi screen already began the server is harmless.
  server.begin();
  Serial.println("Background WebUI started");
  _webuiBgActive = true;
  return true;
}

// Stop the background service. When the WiFi screen is in the
// foreground it keeps owning WiFi and the server, so only the
// background flag is cleared and the screen's own exit cleanup turns
// the radio off. Otherwise the server is stopped and WiFi is powered
// down right away.
void webuiBgStop() {
  if (!_webuiBgActive) {
    return;
  }
  _webuiBgActive = false;
  if (_webuiBgForeground) {
    Serial.println("Background WebUI stopped (WiFi screen keeps WiFi)");
    return;
  }
  server.stop();
  if (_dnsStarted) {
    dnsServer.stop();
    _dnsStarted = false;
  }
  WiFi.mode(WIFI_OFF);
  Serial.println("Background WebUI stopped");
}

// Apply a new setting value immediately, no reboot needed, and store
// it in NVS. Called from the WebUI endpoint and from the WiFi screen
// (BtnA). The NVS write comes first so a crash or power loss cannot
// leave the stored setting disagreeing with the running state.
void webuiBgSetEnabled(bool enabled) {
  NVS.setInt("webui_bg", enabled ? 1 : 0);
  if (enabled) {
    webuiBgStart();
  } else {
    webuiBgStop();
  }
}

// Non-blocking background service tick: serve one HTTP client pass
// and answer captive portal DNS while in AP mode. Called from the
// shared appExitRequested() tick (every app loop) and from the home
// screen loop. Does nothing while the service is off.
void webuiBgTick() {
  if (!_webuiBgActive) {
    return;
  }
  if (_dnsStarted) {
    dnsServer.processNextRequest();
  }
  server.handleClient();
}

// Boot hook, called from setup() after the endpoints are registered:
// bring the service up when the stored setting says so. PIN checks on
// the endpoints still apply, and the device lock screen already ran,
// so starting the server here does not bypass the PIN.
void webuiBgAutoStart() {
  if (webuiBgEnabled()) {
    webuiBgStart();
  }
}

#endif
