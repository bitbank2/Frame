// TRMNL cloud API client — /api/setup and /api/display endpoints.
// Request structure matches the official TRMNL firmware:
//   lib/trmnl/src/api-client/request_headers.cpp  (buildSetupHeaders, buildDisplayHeaders)
//   src/api-client/setup.cpp                       (fetchApiSetup)
//   src/api-client/display.cpp                     (fetchApiDisplay)

#include "api_client.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Simple blocking GET; returns HTTP status code and body string.
static int httpGet(const String& url,
                   std::initializer_list<std::pair<const char*, String>> headers,
                   String& bodyOut)
{
    bool isHttps = url.startsWith("https");
    WiFiClient* client = nullptr;
    if (isHttps) {
        WiFiClientSecure* sc = new WiFiClientSecure();
        sc->setInsecure();
        client = sc;
    } else {
        client = new WiFiClient();
    }

    HTTPClient http;
    if (!http.begin(*client, url)) {
        delete client;
        bodyOut = "http.begin() failed";
        return -1;
    }
    http.setReuse(false);
    http.setTimeout(15000);
    http.setConnectTimeout(15000);
    for (auto& h : headers)
        http.addHeader(h.first, h.second);

    int code = http.GET();
    bodyOut  = (code > 0) ? http.getString() : http.errorToString(code);
    http.end();
    client->stop();
    delete client;

    Serial.printf("[API] %d  %s\n", code, url.c_str());
    return code;
}

// ── GET /api/setup ────────────────────────────────────────────────────────────

TrmnlSetupResult trmnlSetup(const String& baseUrl)
{
    String mac = WiFi.macAddress();
    Serial.printf("[API] setup  ID=%s\n", mac.c_str());

    String body;
    int httpCode = httpGet(baseUrl + "/api/setup",
        // Headers: ID, Content-Type, FW-Version, Model  (from buildSetupHeaders())
        {
            { "ID",           mac              },
            { "Content-Type", "application/json" },
            { "FW-Version",   TRMNL_FW_VERSION  },
            { "Model",        TRMNL_MODEL       },
        }, body);

    if (httpCode <= 0)
        return { false, httpCode, "", "", "" };

    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        Serial.println("[API] setup: JSON parse error");
        return { false, -2, "", "", "" };
    }

    int status = doc["status"] | 0;
    Serial.printf("[API] setup status=%d\n", status);

    if (status != 200)
        return { false, status, "", "", doc["message"] | "" };

    return {
        .ok          = true,
        .status      = status,
        .api_key     = doc["api_key"]     | "",
        .friendly_id = doc["friendly_id"] | "",
        .image_url   = doc["image_url"]   | "",
    };
}

// ── GET /api/display ──────────────────────────────────────────────────────────

TrmnlDisplayResult trmnlDisplay(const String& api_key,
                                uint16_t battery_voltage_mV,
                                const String& baseUrl)
{
    String mac  = WiFi.macAddress();
    String rssi = String(WiFi.RSSI());

    // Format voltage as "V.vv" (e.g. 3842 mV → "3.84"); fall back to "0" if unknown.
    char voltBuf[8] = "0";
    if (battery_voltage_mV > 0)
        snprintf(voltBuf, sizeof(voltBuf), "%u.%02u",
                 battery_voltage_mV / 1000,
                 (battery_voltage_mV % 1000) / 10);

    Serial.printf("[API] display  ID=%s RSSI=%s Battery=%smV\n",
                  mac.c_str(), rssi.c_str(), voltBuf);

    String body;
    int httpCode = httpGet(baseUrl + "/api/display",
        {
            { "ID",              mac               },
            { "Access-Token",    api_key           },
            { "Refresh-Rate",    "900"             },
            { "Battery-Voltage", voltBuf           },
            { "FW-Version",      TRMNL_FW_VERSION  },
            { "RSSI",            rssi              },
            { "Content-Type",    "application/json"},
        }, body);

    if (httpCode <= 0)
        return { false, httpCode, "", 900, false, false, "" };

    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        Serial.println("[API] display: JSON parse error");
        return { false, -2, "", 900, false, false, "" };
    }

    // refresh_rate comes back as a string in the API
    uint32_t refresh = 900;
    if (doc["refresh_rate"].is<const char*>())
        refresh = (uint32_t)atoi(doc["refresh_rate"] | "900");
    else
        refresh = doc["refresh_rate"] | 900u;

    int status = doc["status"] | 0;
    String image_url = doc["image_url"] | "";
    Serial.printf("[API] display status=%d image_url=%s\n", status, image_url.c_str());

    if (image_url.isEmpty())
        return { false, status, "", refresh, false, false, "" };

    return {
        .ok              = true,
        .status          = status,
        .image_url       = image_url,
        .refresh_rate    = refresh,
        .reset_firmware  = doc["reset_firmware"]  | false,
        .update_firmware = doc["update_firmware"] | false,
        .firmware_url    = doc["firmware_url"]    | "",
    };
}
