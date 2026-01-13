#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"
#include "display.h"

// Forward declarations
class Storage;
class Motor;
class Battery;

// Callback types for actions that webserver triggers
typedef void (*ThrowCallback)();
typedef void (*TimeUpdateCallback)(uint32_t epoch);

class FeedMeWebServer {
public:
    void begin();
    void stop();

    bool isRunning() const { return running; }

    // Update last activity timestamp and reset WiFi idle timer
    void recordActivity();
    uint32_t getLastActivityTime() const { return lastActivityTime; }

    // Set callbacks
    void setThrowCallback(ThrowCallback cb) { throwCallback = cb; }
    void setTimeUpdateCallback(TimeUpdateCallback cb) { timeUpdateCallback = cb; }

private:
    AsyncWebServer* server = nullptr;
    bool running = false;
    uint32_t lastActivityTime = 0;

    ThrowCallback throwCallback = nullptr;
    TimeUpdateCallback timeUpdateCallback = nullptr;

    void setupRoutes();
    void setupStaticFiles();
    void setupAPI();
    void setupCaptivePortal();

    // API handlers
    void handleGetStatus(AsyncWebServerRequest* request);
    void handleGetSchedules(AsyncWebServerRequest* request);
    void handleCreateSchedule(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    void handleUpdateSchedule(AsyncWebServerRequest* request, uint8_t* data, size_t len, uint16_t id);
    void handleDeleteSchedule(AsyncWebServerRequest* request, uint16_t id);
    void handleThrow(AsyncWebServerRequest* request);
    void handleTimeSync(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    void handleGetSettings(AsyncWebServerRequest* request);
    void handleUpdateSettings(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    void handleVacationMode(AsyncWebServerRequest* request, uint8_t* data, size_t len);

    // Helper to send JSON response
    void sendJson(AsyncWebServerRequest* request, int code, const String& json);
    void sendError(AsyncWebServerRequest* request, int code, const char* message);
};

extern FeedMeWebServer webServer;
