// Copy this file, rename the copy to secrets.h, and fill in your own values.
// secrets.h stays on your computer only (it is listed in .gitignore).
#pragma once

// Your phone hotspot (gives the ESP32 internet for Supabase).
// In Wokwi use:  "Wokwi-GUEST"  and  ""
const char* HOTSPOT_NAME     = "NOTE 50 Pro+";
const char* HOTSPOT_PASSWORD = "ed_122503";

// Supabase: Project Settings -> API Keys / Data API
const char* SUPABASE_URL = "https://xgucrsnfjbnxbgdgnjvk.supabase.co";
const char* SUPABASE_KEY = "sb_publishable_APHNcWA-bZKF4jFbgBej4g_ZCgqEX5j";

// The ESP32's own WiFi network for the dashboard laptop/tablet.
// Password must be at least 8 characters.
const char* BOOTH_WIFI_NAME     = "TargetFrenzy";
const char* BOOTH_WIFI_PASSWORD = "carnival123";