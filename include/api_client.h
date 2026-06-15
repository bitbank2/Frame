#pragma once
#include <Arduino.h>
#include "config.h"

// ── TRMNL cloud API (GET /api/setup + GET /api/display) ──────────────────────
// Header/field names match the official TRMNL firmware and API documentation.

struct TrmnlSetupResult {
    bool   ok;
    int    status;       // HTTP/JSON status from server (200 = success, 404 = not registered)
    String api_key;
    String friendly_id;
    String image_url;    // optional first image returned by setup
};

struct TrmnlDisplayResult {
    bool     ok;
    int      status;           // JSON status field (0 = success, 202 = no plugin attached)
    String   image_url;
    uint32_t refresh_rate;     // seconds
    bool     reset_firmware;
    bool     update_firmware;
    String   firmware_url;
};

// GET /api/setup  — register device, obtain api_key.
// Only header required: ID (MAC address).
// WiFi must be connected before calling.
TrmnlSetupResult  trmnlSetup(const String& baseUrl = TRMNL_BASE_URL);

// GET /api/display  — fetch current image URL for this device.
// battery_voltage_mV: cell voltage from BQ27427; sent as "Battery-Voltage" header (in V).
// WiFi must be connected before calling.
TrmnlDisplayResult trmnlDisplay(const String& api_key,
                                uint16_t battery_voltage_mV = 0,
                                const String& baseUrl = TRMNL_BASE_URL);
