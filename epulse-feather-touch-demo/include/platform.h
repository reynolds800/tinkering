// What the apps need from whatever they are running on. Implemented in
// src/esp32/ for the device and src/native/ for the simulator.
#pragma once

#include <ArduinoJson.h>
#include <functional>
#include <string>

// Starts joining the network and setting the clock, without waiting for either.
void platformBegin();

// Name of the network being joined, for status messages.
std::string platformNetworkName();

bool platformOnline();

// How full the battery is, from 0 to 100, or -1 if that is not known.
int platformBatteryPercent();

// What this device calls itself to others on the network.
std::string platformDeviceName();

// Local-network chat. Send broadcasts a small packet to everything on the
// network, and also sends it directly to anyone a packet has been received
// from, which reaches them even on a different part of the network. Receive
// collects one packet that has arrived, returning false if none has.
bool platformChatSend(const std::string &packet);
bool platformChatReceive(std::string &packet);

// Small settings that survive a restart, such as the chosen GitHub user.
// Loading a setting that was never saved gives an empty string.
std::string platformLoadSetting(const char *key);
void platformSaveSetting(const char *key, const std::string &value);

// Downloads a web page, handing it to onData a piece at a time so that large
// pages need not fit in memory. onData returns false to stop early. Returns
// the HTTP status code, or -1 if the server could not be reached.
int platformHttpGet(const std::string &url,
                    const std::function<bool(const char *data, size_t size)> &onData);

// Downloads a whole file, such as an image, into body. Repeated downloads
// from the same server reuse one connection where they can, which is much
// quicker. Returns the HTTP status code, or -1 if the server could not be
// reached.
int platformDownload(const std::string &url, std::string &body);

// Fetches a web address that answers in JSON into doc, keeping only the
// fields named in filter. Returns the HTTP status code, -1 if the server
// could not be reached, or -2 if the reply could not be parsed.
int platformJsonGet(const std::string &url, JsonDocument &doc, const JsonDocument &filter);
