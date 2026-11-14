// Copy this file to config.h (which is ignored by git) and fill in your details.
#pragma once

#define WIFI_SSID "your-wifi-name"
#define WIFI_PASSWORD "your-wifi-password"

// GitHub login whose public activity is shown.
#define GITHUB_USER "octocat"

// How often to refresh. Unauthenticated GitHub API calls are limited to 60 per
// hour per IP address and each refresh makes two calls.
#define REFRESH_MINUTES 5
