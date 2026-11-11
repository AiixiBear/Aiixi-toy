// Shared on-demand network access for apps that need an uplink.
//
// The WiFi setup screen owns its own connection while it runs; apps that
// need the network connect here instead, fetch, and disconnect again, so
// the radio is only on while it is actually used. All helpers are
// bounded: a dead network costs seconds, never a hung device.

#ifndef NET_H
#define NET_H

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// Connect to the saved STA network (stored by the WebUI WiFi settings).
// Returns true once an uplink is up. If the device is already
// connected, this returns immediately. No SSID stored means no uplink.
// While the background WebUI is active in AP mode there is no uplink
// and the radio must not be switched to STA: the apps fall back to
// their NVS cache instead.
bool netStaConnect() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }
  if (webuiBgActive() && Wifi_Mode == "AP") {
    return false;
  }
  String ssid = NVS.getString("wifi_ssid");
  String pass = NVS.getString("wifi_password");
  if (ssid == "") {
    return false;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > 10000) {
      return false;
    }
    delay(250);
  }
  return true;
}

// Drop the connection and power the radio down. Skipped while the
// background WebUI is active: the apps reuse the connection it keeps
// up instead of tearing it down after every fetch.
void netDisconnect() {
  if (webuiBgActive()) {
    return;
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

// HTTPS GET, response body on success, false on any failure (DNS,
// connect, TLS, HTTP status, timeout). Certificate validation is
// skipped: the device carries no root CA store, and these are public,
// read-only data endpoints. The trade-off is documented in the README.
bool netHttpsGet(const String& url, String& body, int timeoutMs = 8000) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(timeoutMs);
  if (!http.begin(client, url)) {
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }
  body = http.getString();
  http.end();
  return true;
}

// HTTPS GET that leaves the response stream open for the caller, for
// bodies too large to hold in a String (Uptime Kuma heartbeats). The
// caller owns http and client and must call http.end() when done.
// Returns the HTTP status code, or a negative value on transport error.
// http.getSize() gives the body length when the server sent one.
int netHttpsGetStream(const String& url, HTTPClient& http, WiFiClientSecure& client, int timeoutMs = 15000) {
  client.setInsecure();
  http.setTimeout(timeoutMs);
  if (!http.begin(client, url)) {
    return -1;
  }
  int code = http.GET();
  if (code <= 0) {
    http.end();
    return -1;
  }
  return code;
}

#endif
