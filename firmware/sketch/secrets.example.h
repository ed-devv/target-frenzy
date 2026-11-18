// Copy this file, rename the copy to secrets.h, and fill in your own values.
// secrets.h stays on your computer only (it is listed in .gitignore).
#pragma once

// Your phone hotspot (gives the ESP32 internet for Supabase).
// In Wokwi use:  "Wokwi-GUEST"  and  ""
const char* HOTSPOT_NAME     = "YOUR_HOTSPOT_NAME";
const char* HOTSPOT_PASSWORD = "YOUR_HOTSPOT_PASSWORD";

// Supabase: Project Settings -> API Keys / Data API
const char* SUPABASE_URL = "https://YOUR-PROJECT-ID.supabase.co";
const char* SUPABASE_KEY = "YOUR_SUPABASE_PUBLISHABLE_OR_ANON_KEY";

// The ESP32's own WiFi network for the dashboard laptop/tablet.
// Password must be at least 8 characters.
const char* BOOTH_WIFI_NAME     = "TargetFrenzy";
const char* BOOTH_WIFI_PASSWORD = "carnival123";
