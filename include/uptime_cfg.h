// Uptime Kuma settings, shared by the uptime app and the WebUI
// endpoints. The slug is discovered automatically from /api/entry-page
// when it is not set, so the default setup needs no configuration.

#ifndef UPTIME_CFG_H
#define UPTIME_CFG_H

// NVS keys for the Uptime Kuma status page.
#define UP_KEY_BASE "up_base"
#define UP_KEY_SLUG "up_slug"

#define UP_DEFAULT_BASE "https://uptime.aiixi.cc"

String upGetBase() {
  String v = NVS.getString(UP_KEY_BASE);
  return v == "" ? UP_DEFAULT_BASE : v;
}

String upGetSlug() {
  return NVS.getString(UP_KEY_SLUG);
}

#endif
