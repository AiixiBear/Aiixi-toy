// Persistent weather/air cache: the last successful fetch is saved in
// NVS as a JSON string, so the data survives leaving the app and
// rebooting the device. The JSON always carries an "epoch" field
// (now() at fetch time); the age of a cached fetch is computed from
// it, and the 15-minute freshness window matches the RAM cache the
// apps already used. This header only handles storage and freshness;
// the WeatherData/AirData <-> JSON mapping lives in app_weather.h and
// app_air.h, next to the structs.

#ifndef WX_CACHE_H
#define WX_CACHE_H

#include <ArduinoJson.h>

#define WX_CACHE_KEY "wx_cache"
#define AIR_CACHE_KEY "air_cache"
#define WX_CACHE_MAX_AGE_S 900UL  // 15 minutes, same as the RAM window
// Epoch below which the clock is treated as unset (2023-11-14). The RTC
// is set from NTP or the WebUI; before that, now() is not believable
// and a cache age cannot be computed.
#define WX_CACHE_CLOCK_SANE_EPOCH 1700000000UL

// Store a JSON cache string in NVS. Returns false for an empty string.
bool wxCachePut(const char* key, const String& json) {
  if (json.length() == 0) {
    return false;
  }
  return NVS.setString(key, json);
}

// Load a JSON cache string from NVS; empty string means no cache.
String wxCacheGet(const char* key) {
  return NVS.getString(key);
}

// "epoch" field of a cache JSON string, or 0 when absent/unparseable.
unsigned long wxCacheEpoch(const String& json) {
  if (json.length() == 0) {
    return 0;
  }
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) {
    return 0;
  }
  return doc["epoch"] | 0UL;
}

// True when the device clock is believable (RTC set from NTP/WebUI).
bool wxCacheClockSane() {
  return now() > (time_t)WX_CACHE_CLOCK_SANE_EPOCH;
}

// True when the cached fetch is still inside the freshness window.
// Requires a sane clock: without one the age cannot be computed and
// the caller must attempt a fetch anyway.
bool wxCacheFresh(unsigned long epoch) {
  if (epoch == 0 || !wxCacheClockSane()) {
    return false;
  }
  long age = (long)now() - (long)epoch;
  return age >= 0 && age <= (long)WX_CACHE_MAX_AGE_S;
}

// Age of a cached fetch in seconds, or -1 when it cannot be computed.
long wxCacheAgeS(unsigned long epoch) {
  if (epoch == 0 || !wxCacheClockSane()) {
    return -1;
  }
  return (long)now() - (long)epoch;
}

#endif
