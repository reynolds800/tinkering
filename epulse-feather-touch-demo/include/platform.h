// What the app needs from whatever it is running on. Implemented in
// src/esp32/ for the device and src/native/ for the simulator.
#pragma once

#include <ArduinoJson.h>
#include <string>

// Name of the network being joined, for status messages.
std::string platformNetworkName();

// Joins the network and sets the clock. Returns false if that failed.
bool platformConnect();

bool platformOnline();

// Fetches a GitHub API path (e.g. "/users/octocat") into doc, keeping only the
// fields named in filter. Returns the HTTP status code, -1 if GitHub could not
// be reached, or -2 if the reply could not be parsed.
int platformGithubGet(const std::string &path, JsonDocument &doc, const JsonDocument &filter);
