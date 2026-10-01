#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "config.h"

// HTTP server exposing a firmware upload endpoint while the WiFi AP is running.
//
// The AP is WPA2-protected with a password only obtainable over an authenticated
// BLE session, so the upload endpoint inherits that gate. BLE is down whenever
// WiFi is up (they share the radio), so the app must read credentials before
// triggering OTA mode.
//
// Endpoints:
//   GET  /         - minimal browser upload form (fallback path)
//   GET  /status   - JSON firmware version and upload progress
//   POST /update   - multipart firmware upload, writes to the inactive OTA slot
class OTAServer {
public:
    enum class State {
        IDLE,
        UPLOADING,
        SUCCESS,
        FAILED
    };

    void begin();
    void start();
    void stop();
    void update();

    bool isRunning() const { return running; }
    State getState() const { return state; }

    // True once, after the app asked (POST /exit) to leave WiFi mode early.
    bool consumeExitRequest();

    // Upload progress as 0-100; 0 when no upload is in flight
    uint8_t getProgressPercent() const;

    const char* getErrorMessage() const { return errorMessage; }

private:
    static constexpr uint16_t OTA_PORT = 80;

    // Delay between responding to the client and rebooting, so the HTTP
    // response actually flushes before the radio goes down
    static constexpr uint32_t REBOOT_DELAY_MS = 1500;

    AsyncWebServer server{OTA_PORT};

    bool initialized = false;
    bool running = false;

    State state = State::IDLE;
    size_t bytesWritten = 0;
    size_t totalBytes = 0;
    uint32_t rebootAt = 0;
    bool exitRequested = false;
    uint32_t lastChunkAt = 0;  // millis() of the last upload data received
    char errorMessage[64] = "";

    void registerRoutes();
    void handleUploadChunk(AsyncWebServerRequest* request, size_t index,
                           uint8_t* data, size_t len, bool final);
    void failUpload(const char* message);
};

extern OTAServer otaServer;
