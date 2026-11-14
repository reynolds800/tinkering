// What the apps need from whatever they are running on. Implemented in
// src/esp32/ for the device and src/native/ for the simulator.
#pragma once

#include <ArduinoJson.h>
#include <string>

// Starts joining the network and setting the clock, without waiting for either.
void platformBegin();

// Name of the network being joined, for status messages.
std::string platformNetworkName();

bool platformOnline();

// What this device calls itself to others on the network.
std::string platformDeviceName();

// Local-network chat. Send broadcasts a small packet to everything on the
// network; receive collects one that has arrived, returning false if none has.
bool platformChatSend(const std::string &packet);
bool platformChatReceive(std::string &packet);

// Fetches a GitHub API path (e.g. "/users/octocat") into doc, keeping only the
// fields named in filter. Returns the HTTP status code, -1 if GitHub could not
// be reached, or -2 if the reply could not be parsed.
int platformGithubGet(const std::string &path, JsonDocument &doc, const JsonDocument &filter);
