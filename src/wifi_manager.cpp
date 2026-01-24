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
        DEBUG_PRINTF("WiFi: Generated new password: %s\n", password);
    }
    prefs.end();

    initialized = true;
    DEBUG_PRINTF("WiFi: Initialized, SSID: %s, Pass: %s\n", ssid, password);
}

void WiFiManager::update() {
    if (!wifiRunning) {
        return;
    }

    // Process DNS requests for captive portal
    if (dnsServer) {
        dnsServer->processNextRequest();
    }

    // Update client count (for informational purposes)
    int currentClients = WiFi.softAPgetStationNum();
    if (currentClients != clientCount) {
        clientCount = currentClients;
        DEBUG_PRINTF("WiFi: %d client(s) connected\n", clientCount);
    }

    // Idle timer is now controlled by heartbeat from web UI
    // Timer starts when WiFi starts (in start()) and resets on heartbeat
    // This allows countdown to work properly in the web interface
}

void WiFiManager::start() {
    if (!initialized || wifiRunning) {
        return;
    }

    setupAP();
    wifiRunning = true;
    idleStartTime = millis();  // Start the idle timer
    DEBUG_PRINTLN("WiFi: AP started, idle timer started");
}

void WiFiManager::stop() {
    if (!wifiRunning) {
        return;
    }

    stopAP();
    wifiRunning = false;
    clientCount = 0;
    DEBUG_PRINTLN("WiFi: AP stopped");
}

uint32_t WiFiManager::getIdleTime() const {
    if (!wifiRunning) {
        return 0;
    }
    return millis() - idleStartTime;
}

bool WiFiManager::shouldAutoStop() const {
    if (!wifiRunning) {
        return false;
    }
    return (millis() - idleStartTime) >= WIFI_IDLE_TIMEOUT_MS;
}

uint32_t WiFiManager::getRemainingIdleSeconds() const {
    if (!wifiRunning) {
        return 0;
    }

    uint32_t elapsed = millis() - idleStartTime;
    if (elapsed >= WIFI_IDLE_TIMEOUT_MS) {
        return 0;
    }

    return (WIFI_IDLE_TIMEOUT_MS - elapsed) / 1000;
}

void WiFiManager::resetIdleTimer() {
    idleStartTime = millis();
}

void WiFiManager::setupAP() {
    // Configure AP
    WiFi.mode(WIFI_AP);

    // Set maximum TX power for better range
    WiFi.setTxPower(WIFI_POWER_19_5dBm);

    // Start AP - channel 1, not hidden, max 4 connections
    bool success = WiFi.softAP(ssid, password, 1, false, 4);

    if (!success) {
        DEBUG_PRINTLN("WiFi: ERROR - Failed to start AP!");
        return;
    }

    // Delay for AP to initialize
    delay(500);

    // Configure AP IP settings after AP is started
    IPAddress localIP(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(localIP, gateway, subnet);

    DEBUG_PRINTF("WiFi: AP started - SSID: %s, Pass: %s\n", ssid, password);
    DEBUG_PRINTF("WiFi: AP IP address: %s\n", WiFi.softAPIP().toString().c_str());

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
