#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include "config.h"

class WiFiManager {
public:
    void begin(const char* deviceId);
    void update();
    void start();
    void stop();

    bool isRunning() const { return wifiRunning; }
    bool hasClients() const { return clientCount > 0; }
    int getClientCount() const { return clientCount; }
    uint32_t getIdleTime() const;

    // Check if WiFi should auto-stop due to idle timeout
    bool shouldAutoStop() const;

    // Get remaining seconds before WiFi auto-stops
    uint32_t getRemainingIdleSeconds() const;

    // Reset idle timer (call when there's activity)
    void resetIdleTimer();

    // Get network info
    const char* getSSID() const { return ssid; }
    const char* getPassword() const { return password; }
    IPAddress getIP() const { return WiFi.softAPIP(); }

private:
    bool wifiRunning = false;
    bool initialized = false;
    uint32_t idleStartTime = 0;
    int clientCount = 0;

    char ssid[32] = "";
    char password[32] = "";

    DNSServer* dnsServer = nullptr;

    void setupAP();
    void stopAP();
};

extern WiFiManager wifiManager;
