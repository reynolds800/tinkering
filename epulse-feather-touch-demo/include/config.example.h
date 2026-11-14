// Copy this file to config.h (which is ignored by git) and fill in your details.
#pragma once

#define WIFI_SSID "your-wifi-name"
#define WIFI_PASSWORD "your-wifi-password"

// GitHub login whose public activity is shown.
#define GITHUB_USER "octocat"

// How often to refresh. Unauthenticated GitHub API calls are limited to 60 per
// hour per IP address and each refresh makes two calls.
#define REFRESH_MINUTES 5

// The name other devices see on messages sent from this one.
#define DEVICE_NAME "ePulse"

// Timezone for the clock, as a POSIX TZ string. Sydney would be
// "AEST-10AEDT,M10.1.0,M4.1.0/3". The simulator uses the computer's timezone.
#define TIMEZONE "UTC0"
