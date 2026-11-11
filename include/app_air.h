// Air quality + UV app: Open-Meteo Air Quality API (free, no key).
//
// Shows US AQI, PM2.5, PM10 and the UV index for the current hour.
// Same location settings as the weather app (NVS wx_lat/wx_lon/wx_name,
// edited in the WebUI). Fetches on entry and on BtnA, caches 15
// minutes. UV index is an hourly variable, so the value for the
// current local hour is picked out of the hourly series; if it cannot
// be found, today's maximum is shown instead and labelled as such.

#ifndef APP_AIR_H
#define APP_AIR_H

#include <ArduinoJson.h>
#include "weather_cfg.h"

#define AIR_CACHE_MS 900000UL  // 15 minutes

// US AQI categories (https://www.airnow.gov/aqi/aqi-basics/).
const char* aqiCategoryText(int aqi) {
  if (aqi <= 50) return "Good";
  if (aqi <= 100) return "Moderate";
  if (aqi <= 150) return "Unhealthy (sensitive)";
  if (aqi <= 200) return "Unhealthy";
  if (aqi <= 300) return "Very unhealthy";
  return "Hazardous";
}

// UV index scale (WHO).
const char* uvCategoryText(float uv) {
  if (uv < 3) return "Low";
  if (uv < 6) return "Moderate";
  if (uv < 8) return "High";
  if (uv < 11) return "Very high";
  return "Extreme";
}

struct AirData {
  bool ok = false;
  float pm25 = 0;
  float pm10 = 0;
  int aqi = -1;       // -1: not reported
  float uv = -1;      // -1: not reported
  bool uvIsMax = false;
  String updated;     // local HH:MM of the fetch
};

bool airFetch(AirData& data, String& error) {
  data.ok = false;
  if (!netStaConnect()) {
    error = "NO WIFI SETUP";
    return false;
  }

  String url = "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=" +
               wxGetLat() + "&longitude=" + wxGetLon() +
               "&current=pm2_5,pm10,us_aqi&hourly=uv_index"
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
  if (doc["current"].isNull() || doc["hourly"].isNull()) {
    error = "BAD RESPONSE";
    return false;
  }

  data.pm25 = doc["current"]["pm2_5"] | 0.0f;
  data.pm10 = doc["current"]["pm10"] | 0.0f;
  data.aqi = doc["current"]["us_aqi"] | -1;

  // UV index: find the hourly entry for the current local hour.
  localTime = M5.Rtc.getDateTime();
  getCurrentLocalTime(&localTime, timezone_h, timezone_m);
  String nowHour = String(localTime.date.year) + "-" +
                   getString_2digit(localTime.date.month) + "-" +
                   getString_2digit(localTime.date.date) + "T" +
                   getString_2digit(localTime.time.hours) + ":00";
  data.uv = -1;
  data.uvIsMax = false;
  JsonArray uvTimes = doc["hourly"]["time"];
  JsonArray uvValues = doc["hourly"]["uv_index"];
  float uvMax = -1;
  for (size_t i = 0; i < uvTimes.size() && i < uvValues.size(); i++) {
    float v = uvValues[i] | -1.0f;
    if (v > uvMax) {
      uvMax = v;
    }
    if (data.uv < 0 && String(uvTimes[i].as<const char*>()) == nowHour) {
      data.uv = v;
    }
  }
  if (data.uv < 0 && uvMax >= 0) {
    data.uv = uvMax;
    data.uvIsMax = true;
  }

  data.updated = getString_2digit(localTime.time.hours) + ":" +
                 getString_2digit(localTime.time.minutes);
  data.ok = true;
  return true;
}

void airDraw(const AirData& data, const String& error, bool fetching) {
  M5.Lcd.fillRect(0, toolbar_height, screen_x, screen_y - toolbar_height, bg_color);
  M5.Lcd.setTextColor(txt_color, bg_color);

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
    // 160x80: AQI, PM values and UV on three lines.
    M5.Lcd.setFont(&beta10pt7b);
    M5.Lcd.setCursor(8, toolbar_height + 26);
    if (data.aqi >= 0) {
      M5.Lcd.print("AQI " + String(data.aqi));
    } else {
      M5.Lcd.print("AQI --");
    }
    M5.Lcd.setFont(&beta5pt7b);
    M5.Lcd.setCursor(8, toolbar_height + 42);
    M5.Lcd.print("PM2.5 " + String(data.pm25, 0) + "  PM10 " + String(data.pm10, 0));
    M5.Lcd.setCursor(8, toolbar_height + 54);
    if (data.uv >= 0) {
      M5.Lcd.print("UV " + String(data.uv, 0) + " " + uvCategoryText(data.uv));
    } else {
      M5.Lcd.print("UV --");
    }
  } else {
    // 240x135: big AQI with category, PM values, UV, updated time.
    M5.Lcd.setFont(&mishmash21pt7b);
    M5.Lcd.setCursor(8, toolbar_height + 32);
    if (data.aqi >= 0) {
      M5.Lcd.print("AQI " + String(data.aqi));
    } else {
      M5.Lcd.print("AQI --");
    }
    M5.Lcd.setFont(&beta10pt7b);
    M5.Lcd.setCursor(8, toolbar_height + 58);
    M5.Lcd.print(data.aqi >= 0 ? aqiCategoryText(data.aqi) : "--");
    M5.Lcd.setFont(&beta8pt7b);
    M5.Lcd.setCursor(8, toolbar_height + 80);
    M5.Lcd.print("PM2.5 " + String(data.pm25, 1) + "  PM10 " + String(data.pm10, 1));
    M5.Lcd.setCursor(8, toolbar_height + 96);
    if (data.uv >= 0) {
      M5.Lcd.print("UV " + String(data.uv, 1) + " " + uvCategoryText(data.uv) +
                   (data.uvIsMax ? " (max)" : ""));
    } else {
      M5.Lcd.print("UV --");
    }
    M5.Lcd.setFont(&beta5pt7b);
    M5.Lcd.setCursor(8, screen_y - 12);
    M5.Lcd.print("UPD " + data.updated + "  A: REFRESH");
  }
}

void air_screen() {
  AirData data;
  String error = "";
  unsigned long lastFetchMs = 0;
  bool needFetch = true;
  bool fetching = false;

  while (!appExitRequested()) {
    if (needFetch) {
      needFetch = false;
      fetching = true;
      airDraw(data, error, true);
      if (airFetch(data, error)) {
        lastFetchMs = millis();
      }
      fetching = false;
      airDraw(data, error, false);
    }

    if (data.ok && millis() - lastFetchMs > AIR_CACHE_MS) {
      needFetch = true;
    }

    if (M5.BtnA.wasClicked()) {
      previousMillis = millis();
      needFetch = true;
    }

    delay(50);
  }
}

#endif
