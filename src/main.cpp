#include <Arduino.h>
#include "config.h"
#include "buttons.h"
#include "display.h"
#include "motor.h"
#include "battery.h"
#include "storage.h"
#include "wifi_manager.h"
#include "webserver.h"

// State tracking
bool webServerActive = false;
bool wifiStartedBySchedule = false;  // Track if WiFi was auto-started by schedule
uint32_t lastStatusUpdate = 0;
uint32_t lastWifiScheduleCheck = 0;
constexpr uint32_t STATUS_UPDATE_INTERVAL = 1000;
constexpr uint32_t WIFI_SCHEDULE_CHECK_INTERVAL = 30000;  // Check every 30 seconds

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== FeedMe ===");

    // Power enable for peripheral power (T-Display-S3)
    pinMode(15, OUTPUT);
    digitalWrite(15, HIGH);
    delay(100);

    // Initialize storage first (needed for device ID)
    Serial.println("Initializing storage...");
    if (!storage.begin()) {
        Serial.println("ERROR: Storage initialization failed!");
    }

    // Initialize display
    Serial.println("Initializing display...");
    display.begin();

    // Initialize buttons
    Serial.println("Initializing buttons...");
    buttons.begin();

    // Initialize battery
    Serial.println("Initializing battery...");
    battery.begin();

    // Initialize motor
    Serial.println("Initializing motor...");
    motor.begin();

    // Initialize WiFi manager
    Serial.println("Initializing WiFi...");
    wifiManager.begin(storage.getDeviceId());

    // Pass WiFi credentials to display for Connectivity screen
    display.setPairingInfo(wifiManager.getSSID(), wifiManager.getPassword());

    Serial.println("Setup complete!");
}

void loop() {
    // Update subsystems
    buttons.update();
    battery.update();
    motor.update();

    // Check WiFi schedules periodically
    uint32_t now = millis();
    if (now - lastWifiScheduleCheck >= WIFI_SCHEDULE_CHECK_INTERVAL) {
        lastWifiScheduleCheck = now;

        bool shouldBeActive = storage.shouldWifiBeActive();
        bool hasClients = wifiManager.getClientCount() > 0;

        if (shouldBeActive && !wifiManager.isRunning()) {
            // Schedule says WiFi should be on, start it
            Serial.println("WiFi schedule active - starting WiFi");
            wifiManager.start();
            wifiStartedBySchedule = true;
        } else if (!shouldBeActive && wifiManager.isRunning() && wifiStartedBySchedule && !hasClients) {
            // Schedule says WiFi should be off, it was auto-started, and no clients connected
            Serial.println("WiFi schedule ended - stopping WiFi");
            wifiManager.stop();
            wifiStartedBySchedule = false;
        }
    }

    // Handle WiFi state
    if (wifiManager.isRunning()) {
        wifiManager.update();

        // Start webserver if WiFi is running but server isn't
        if (!webServerActive) {
            webServer.begin();
            webServerActive = true;
            Serial.println("WebServer started");
        }

        // Check for auto-stop due to idle timeout (only for manually started WiFi, and no clients)
        if (!wifiStartedBySchedule && wifiManager.shouldAutoStop() && wifiManager.getClientCount() == 0) {
            Serial.println("WiFi idle timeout - stopping");
            wifiManager.stop();
        }
    } else if (webServerActive) {
        // WiFi stopped (manually or timeout), stop webserver
        webServer.stop();
        webServerActive = false;
        wifiStartedBySchedule = false;
        Serial.println("WebServer stopped");
    }

    // Handle button events for display navigation
    ButtonEvent event = buttons.getEvent();
    if (event != ButtonEvent::NONE) {
        display.handleButton(event);
    }

    // Update display
    display.update();

    delay(10);
}
