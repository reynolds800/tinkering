// Loads the user's settings from config.h. The simulator falls back to the
// example settings so it can run without one.
#pragma once

#if __has_include("config.h")
#include "config.h"
#elif defined(SIMULATOR)
#include "config.example.h"
#else
#error "Copy include/config.example.h to include/config.h and fill in your WiFi details"
#endif

#ifndef TIMEZONE
#define TIMEZONE "UTC0"
#endif
