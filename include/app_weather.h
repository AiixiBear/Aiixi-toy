// Weather app: current conditions from Open-Meteo (free, no API key).
//
// Fetches on entry and on BtnA press; the result is cached, so leaving
// and re-entering within 15 minutes does not hit the network again.
// Location (latitude, longitude, display name) is stored in NVS and
// edited in the WebUI; the default is Taipei. The API timezone is
// "auto", so daily high/low follow the location, not the device.
//
// A fetch blocks for a few seconds while the radio is up; the exit
// gesture (BtnB held) is only noticed once it returns.

#ifndef APP_WEATHER_H
#define APP_WEATHER_H

#include <ArduinoJson.h>
#include "weather_cfg.h"

// WMO weather codes (https://open-meteo.com/en/docs).
const char* wmoWeatherText(int code) {
  switch (code) {
    case 0: return "Clear sky";
    case 1: return "Mainly clear";
    case 2: return "Partly cloudy";
    case 3: return "Overcast";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: case 63: case 65: return "Rain";
    case 66: case 67: return "Freezing rain";
    case 71: case 73: case 75: return "Snow";
    case 77: return "Snow grains";
    case 80: case 81: case 82: return "Rain showers";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Storm w/ hail";
    default: return "Unknown";
  }
}

struct WeatherData {
  bool ok = false;
  float temp = 0;
  float tempMax = 0;
  float tempMin = 0;
  int humidity = 0;
  int rainChance = -1;  // -1: not reported
  int code = -1;
  String updated;       // local HH:MM of the fetch
};

#define WX_CACHE_MS 900000UL  // 15 minutes

// Fetch current conditions. Returns false with `error` set on failure.
bool wxFetch(WeatherData& data, String& error) {
  data.ok = false;
  if (!netStaConnect()) {
    error = "NO WIFI SETUP";
    return false;
  }

  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + wxGetLat() +
               "&longitude=" + wxGetLon() +
               "&current=temperature_2m,relative_humidity_2m,weather_code"
               "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max"
               "&timezone=auto&forecast_days=1";
  String body;
  if (!netHttpsGet(url, body)) {
    netDisconnect();
    error = "FETCH FAILED";
    return false;
  }
  netDisconnect();

  JsonDocument doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    error = "BAD RESPONSE";
    return false;
  }
  if (doc["current"].isNull() || doc["daily"].isNull()) {
    error = "BAD RESPONSE";
    return false;
  }

  data.temp = doc["current"]["temperature_2m"] | 0.0f;
  data.humidity = doc["current"]["relative_humidity_2m"] | 0;
  data.code = doc["current"]["weather_code"] | -1;
  data.tempMax = doc["daily"]["temperature_2m_max"][0] | 0.0f;
  data.tempMin = doc["daily"]["temperature_2m_min"][0] | 0.0f;
  data.rainChance = doc["daily"]["precipitation_probability_max"][0] | -1;

  localTime = M5.Rtc.getDateTime();
  getCurrentLocalTime(&localTime, timezone_h, timezone_m);
  data.updated = getString_2digit(localTime.time.hours) + ":" +
                 getString_2digit(localTime.time.minutes);
  data.ok = true;
  return true;
}

void wxDraw(const WeatherData& data, const String& error, bool fetching) {
  M5.Lcd.fillRect(0, toolbar_height, screen_x, screen_y - toolbar_height, bg_color);
  M5.Lcd.setTextColor(txt_color, bg_color);

  // Location line.
  M5.Lcd.setFont(&beta8pt7b);
  M5.Lcd.setCursor(8, toolbar_height + 12);
  M5.Lcd.print(wxGetName());

  if (fetching) {
    M5.Lcd.setFont(&beta8pt7b);
    M5.Lcd.setCursor(8, toolbar_height + 40);
    M5.Lcd.print("UPDATING...");
    return;
  }

  if (!data.ok) {
    M5.Lcd.setFont(&beta8pt7b);
    M5.Lcd.setCursor(8, toolbar_height + 40);
    M5.Lcd.print(error);
    M5.Lcd.setFont(&beta5pt7b);
    M5.Lcd.setCursor(8, toolbar_height + 58);
    M5.Lcd.print("A: RETRY  B HOLD: HOME");
    return;
  }

  if (current_screen == STICKC) {
    // 160x80: temperature, condition, high/low on three lines.
    M5.Lcd.setFont(&beta15pt7b);
    M5.Lcd.setCursor(8, toolbar_height + 28);
    M5.Lcd.print(String(data.temp, 0) + "C");
    M5.Lcd.setFont(&beta5pt7b);
    M5.Lcd.setCursor(8, toolbar_height + 44);
    M5.Lcd.print(wmoWeatherText(data.code));
    M5.Lcd.setCursor(8, toolbar_height + 56);
    M5.Lcd.print("H:" + String(data.tempMax, 0) + " L:" + String(data.tempMin, 0) +
                 "  " + String(data.humidity) + "%");
  } else {
    // 240x135: large temperature, condition, details, updated time.
    M5.Lcd.setFont(&mishmash21pt7b);
    M5.Lcd.setCursor(8, toolbar_height + 32);
    M5.Lcd.print(String(data.temp, 0) + "C");
    M5.Lcd.setFont(&beta10pt7b);
    M5.Lcd.setCursor(8, toolbar_height + 58);
    M5.Lcd.print(wmoWeatherText(data.code));
    M5.Lcd.setFont(&beta8pt7b);
    M5.Lcd.setCursor(8, toolbar_height + 80);
    M5.Lcd.print("H:" + String(data.tempMax, 0) + "  L:" + String(data.tempMin, 0) +
                 "  Hum " + String(data.humidity) + "%");
    M5.Lcd.setCursor(8, toolbar_height + 96);
    if (data.rainChance >= 0) {
      M5.Lcd.print("Rain " + String(data.rainChance) + "%");
    } else {
      M5.Lcd.print("Rain --%");
    }
    M5.Lcd.setFont(&beta5pt7b);
    M5.Lcd.setCursor(8, screen_y - 12);
    M5.Lcd.print("UPD " + data.updated + "  A: REFRESH");
  }
}

void weather_screen() {
  WeatherData data;
  String error = "";
  unsigned long lastFetchMs = 0;
  bool needFetch = true;
  bool fetching = false;

  while (!appExitRequested()) {
    if (needFetch) {
      needFetch = false;
      fetching = true;
      wxDraw(data, error, true);
      if (wxFetch(data, error)) {
        lastFetchMs = millis();
      }
      fetching = false;
      wxDraw(data, error, false);
    }

    // Refresh in the background when the cache is stale.
    if (data.ok && millis() - lastFetchMs > WX_CACHE_MS) {
      needFetch = true;
    }

    // BtnA refreshes now.
    if (M5.BtnA.wasClicked()) {
      previousMillis = millis();
      needFetch = true;
    }

    delay(50);
  }
}

#endif
