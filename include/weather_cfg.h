// Weather location settings, shared by the weather app and the WebUI
// endpoints. Stored in NVS as strings so no float handling is needed in
// the settings endpoints.

#ifndef WEATHER_CFG_H
#define WEATHER_CFG_H

// NVS keys for the weather location.
#define WX_KEY_LAT "wx_lat"
#define WX_KEY_LON "wx_lon"
#define WX_KEY_NAME "wx_name"

#define WX_DEFAULT_LAT "25.03"
#define WX_DEFAULT_LON "121.56"
#define WX_DEFAULT_NAME "Taipei"

String wxGetLat() {
  String v = NVS.getString(WX_KEY_LAT);
  return v == "" ? WX_DEFAULT_LAT : v;
}

String wxGetLon() {
  String v = NVS.getString(WX_KEY_LON);
  return v == "" ? WX_DEFAULT_LON : v;
}

String wxGetName() {
  String v = NVS.getString(WX_KEY_NAME);
  return v == "" ? WX_DEFAULT_NAME : v;
}

#endif
