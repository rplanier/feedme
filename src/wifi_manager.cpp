#include "wifi_manager.h"
#include <Preferences.h>

WiFiManager wifiManager;

void WiFiManager::begin(const char* deviceId) {
    // Build SSID
    snprintf(ssid, sizeof(ssid), "%s%s", WIFI_SSID_PREFIX, deviceId);

    // Load or generate random password
    Preferences prefs;
    prefs.begin("wifi", false);
    String savedPass = prefs.getString("password", "");

    if (savedPass.length() == 8) {
        strncpy(password, savedPass.c_str(), sizeof(password));
    } else {
        // Generate random 8-char alphanumeric password
        const char charset[] = "0123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghjkmnpqrstuvwxyz";
        randomSeed(esp_random());
        for (int i = 0; i < 8; i++) {
            password[i] = charset[random(0, sizeof(charset) - 1)];
        }
        password[8] = '\0';
        prefs.putString("password", password);
        Serial.printf("WiFi: Generated new password: %s\n", password);
    }
    prefs.end();

    initialized = true;
    Serial.printf("WiFi: Initialized, SSID: %s, Pass: %s\n", ssid, password);
}

void WiFiManager::update() {
    if (!wifiRunning) {
        return;
    }

    // Process DNS requests for captive portal
    if (dnsServer) {
        dnsServer->processNextRequest();
    }

    // Update client count
    int currentClients = WiFi.softAPgetStationNum();
    if (currentClients != clientCount) {
        clientCount = currentClients;
        Serial.printf("WiFi: %d client(s) connected\n", clientCount);
    }

    // Track idle state (no WiFi clients)
    if (clientCount > 0) {
        // Clients connected, reset idle timer
        resetIdleTimer();
    } else if (idleStartTime == 0) {
        // Just became idle, start the timer
        idleStartTime = millis();
        Serial.println("WiFi: No clients, starting idle timer");
    }
}

void WiFiManager::start() {
    if (!initialized || wifiRunning) {
        return;
    }

    setupAP();
    wifiRunning = true;
    resetIdleTimer();
    Serial.println("WiFi: AP started");
}

void WiFiManager::stop() {
    if (!wifiRunning) {
        return;
    }

    stopAP();
    wifiRunning = false;
    clientCount = 0;
    Serial.println("WiFi: AP stopped");
}

uint32_t WiFiManager::getIdleTime() const {
    if (!wifiRunning || idleStartTime == 0) {
        return 0;
    }
    return millis() - idleStartTime;
}

bool WiFiManager::shouldAutoStop() const {
    if (!wifiRunning || idleStartTime == 0) {
        return false;
    }
    return (millis() - idleStartTime) >= WIFI_IDLE_TIMEOUT_MS;
}

void WiFiManager::resetIdleTimer() {
    idleStartTime = 0;
}

void WiFiManager::setupAP() {
    // Configure AP
    WiFi.mode(WIFI_AP);

    // Configure AP with explicit settings for better iOS compatibility
    // Channel 1, no hidden SSID, max 4 connections
    bool success = WiFi.softAP(ssid, password, 1, false, 4);

    if (!success) {
        Serial.println("WiFi: ERROR - Failed to start AP!");
        return;
    }

    // Longer delay for AP to fully initialize
    delay(500);

    // Configure AP IP settings
    IPAddress localIP(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(localIP, gateway, subnet);

    Serial.printf("WiFi: AP started - SSID: %s, Pass: %s\n", ssid, password);
    Serial.printf("WiFi: AP IP address: %s\n", WiFi.softAPIP().toString().c_str());

    // Setup DNS server for captive portal
    dnsServer = new DNSServer();
    dnsServer->start(53, "*", WiFi.softAPIP());
}

void WiFiManager::stopAP() {
    if (dnsServer) {
        dnsServer->stop();
        delete dnsServer;
        dnsServer = nullptr;
    }

    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
}
