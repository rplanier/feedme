#include "webserver.h"
#include "storage.h"
#include "motor.h"
#include "battery.h"
#include "wifi_manager.h"
#include "config.h"
#include "rtc_manager.h"

WebServer webServer;

void WebServer::recordActivity() {
    lastActivityTime = millis();
    wifiManager.resetIdleTimer();  // Any API activity resets WiFi timeout
}

void WebServer::begin() {
    if (running) {
        return;
    }

    server = new AsyncWebServer(80);
    setupRoutes();

    server->begin();
    running = true;
    lastActivityTime = millis();

    Serial.println("WebServer: Started on port 80");
}

void WebServer::stop() {
    if (!running) {
        return;
    }

    server->end();
    delete server;
    server = nullptr;
    running = false;

    Serial.println("WebServer: Stopped");
}

void WebServer::setupRoutes() {
    setupStaticFiles();
    setupAPI();

    // Captive portal redirect
    server->onNotFound([this](AsyncWebServerRequest* request) {
        recordActivity();
        request->redirect("/");
    });
}

void WebServer::setupStaticFiles() {
    // Serve index.html for root
    server->on("/", HTTP_GET, [this](AsyncWebServerRequest* request) {
        recordActivity();
        if (LittleFS.exists("/index.html")) {
            request->send(LittleFS, "/index.html", "text/html");
        } else {
            // Fallback if no file uploaded yet
            request->send(200, "text/html",
                "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>FeedMe</title></head><body>"
                "<h1>FeedMe</h1>"
                "<p>Web interface files not uploaded yet.</p>"
                "<p>Use 'pio run --target uploadfs' to upload.</p>"
                "</body></html>");
        }
    });

    // Serve static files from LittleFS
    server->serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
}

void WebServer::setupAPI() {
    // GET /api/status
    server->on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetStatus(request);
    });

    // GET /api/schedules
    server->on("/api/schedules", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetSchedules(request);
    });

    // POST /api/schedules/update - Update schedule
    server->on("/api/schedules/update", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index == 0) {
                recordActivity();
                if (request->hasParam("id")) {
                    uint16_t id = request->getParam("id")->value().toInt();
                    Serial.printf("WebServer: Updating schedule ID %d\n", id);
                    handleUpdateSchedule(request, data, len, id);
                } else {
                    sendError(request, 400, "Missing schedule ID");
                }
            }
        }
    );

    // POST /api/schedules/delete - Delete schedule
    server->on("/api/schedules/delete", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index == 0) {
                recordActivity();
                if (request->hasParam("id")) {
                    uint16_t id = request->getParam("id")->value().toInt();
                    Serial.printf("WebServer: Deleting schedule ID %d\n", id);
                    handleDeleteSchedule(request, id);
                } else {
                    sendError(request, 400, "Missing schedule ID");
                }
            }
        }
    );

    // POST /api/schedules - Create new schedule
    server->on("/api/schedules", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index == 0) {
                Serial.println("WebServer: Creating new schedule");
                handleCreateSchedule(request, data, len);
            }
        }
    );

    // POST /api/throw
    server->on("/api/throw", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleThrow(request);
    });

    // GET /api/heartbeat - Keep WiFi alive (recordActivity already resets timer)
    server->on("/api/heartbeat", HTTP_GET, [this](AsyncWebServerRequest* request) {
        recordActivity();
        request->send(200, "application/json", "{\"ok\":true}");
    });

    // POST /api/time
    server->on("/api/time", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index == 0) {
                handleTimeSync(request, data, len);
            }
        }
    );

    // GET /api/settings
    server->on("/api/settings", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetSettings(request);
    });

    // POST /api/settings
    server->on("/api/settings", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index == 0) {
                handleUpdateSettings(request, data, len);
            }
        }
    );

    // POST /api/vacation
    server->on("/api/vacation", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index == 0) {
                handleVacationMode(request, data, len);
            }
        }
    );

    // GET /api/ble-schedules
    server->on("/api/ble-schedules", HTTP_GET, [this](AsyncWebServerRequest* request) {
        recordActivity();
        String json = storage.getBleSchedulesJson();
        sendJson(request, 200, json);
    });

    // POST /api/ble-schedules/update - Update BLE schedule
    server->on("/api/ble-schedules/update", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index == 0) {
                recordActivity();
                if (!request->hasParam("id")) {
                    sendError(request, 400, "Missing schedule ID");
                    return;
                }
                uint16_t id = request->getParam("id")->value().toInt();
                Serial.printf("WebServer: Updating BLE schedule ID %d\n", id);

                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, data, len);
                if (error) {
                    sendError(request, 400, "Invalid JSON");
                    return;
                }

                BleSchedule* existing = storage.getBleScheduleById(id);
                if (!existing) {
                    sendError(request, 404, "BLE schedule not found");
                    return;
                }

                BleSchedule schedule = *existing;
                if (doc.containsKey("name")) strlcpy(schedule.name, doc["name"], sizeof(schedule.name));
                if (doc.containsKey("startHour")) schedule.startHour = doc["startHour"];
                if (doc.containsKey("startMinute")) schedule.startMinute = doc["startMinute"];
                if (doc.containsKey("endHour")) schedule.endHour = doc["endHour"];
                if (doc.containsKey("endMinute")) schedule.endMinute = doc["endMinute"];
                if (doc.containsKey("days")) schedule.days = doc["days"];
                if (doc.containsKey("enabled")) schedule.enabled = doc["enabled"];

                if (storage.updateBleSchedule(id, schedule)) {
                    sendJson(request, 200, "{\"success\":true}");
                } else {
                    sendError(request, 500, "Failed to update BLE schedule");
                }
            }
        }
    );

    // POST /api/ble-schedules/delete - Delete BLE schedule
    server->on("/api/ble-schedules/delete", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index == 0) {
                recordActivity();
                if (request->hasParam("id")) {
                    uint16_t id = request->getParam("id")->value().toInt();
                    Serial.printf("WebServer: Deleting BLE schedule ID %d\n", id);
                    if (storage.deleteBleSchedule(id)) {
                        sendJson(request, 200, "{\"success\":true}");
                    } else {
                        sendError(request, 404, "BLE schedule not found");
                    }
                } else {
                    sendError(request, 400, "Missing schedule ID");
                }
            }
        }
    );

    // POST /api/ble-schedules - Create new BLE schedule (must be AFTER specific routes)
    server->on("/api/ble-schedules", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index == 0) {
                recordActivity();
                Serial.println("WebServer: Creating new BLE schedule");
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, data, len);
                if (error) {
                    sendError(request, 400, "Invalid JSON");
                    return;
                }

                BleSchedule schedule = {};
                strlcpy(schedule.name, doc["name"] | "BLE Window", sizeof(schedule.name));
                schedule.startHour = doc["startHour"] | 4;
                schedule.startMinute = doc["startMinute"] | 0;
                schedule.endHour = doc["endHour"] | 20;
                schedule.endMinute = doc["endMinute"] | 0;
                schedule.days = doc["days"] | 0x7F;
                schedule.enabled = doc["enabled"] | true;

                if (storage.addBleSchedule(schedule)) {
                    sendJson(request, 201, "{\"success\":true}");
                } else {
                    sendError(request, 500, "Failed to add BLE schedule");
                }
            }
        }
    );
}

void WebServer::handleGetStatus(AsyncWebServerRequest* request) {
    recordActivity();

    JsonDocument doc;
    doc["version"] = FEEDME_VERSION;
    doc["batteryVoltage"] = battery.getVoltage();
    doc["batteryStatus"] = battery.getStatusText();
    doc["isCharging"] = battery.isCharging();
    doc["vacationMode"] = storage.getSettings().vacationMode;
    doc["motorRunning"] = motor.isRunning();
    doc["deviceId"] = storage.getDeviceId();
    doc["timeSynced"] = rtcManager.isTimeSynced();

    // Current time from RTC manager
    DateTime now = rtcManager.now();
    char timeStr[32];
    snprintf(timeStr, sizeof(timeStr), "%04d-%02d-%02dT%02d:%02d:%02d",
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());
    doc["currentTime"] = timeStr;

    // Next feed time (only if time is synced)
    if (rtcManager.isTimeSynced()) {
        int nextHour, nextMinute, daysAway;
        if (storage.getNextRunTime(nextHour, nextMinute, daysAway)) {
            char nextFeedStr[32];
            static const char* dayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

            if (daysAway == 0) {
                snprintf(nextFeedStr, sizeof(nextFeedStr), "Today %02d:%02d", nextHour, nextMinute);
            } else if (daysAway == 1) {
                snprintf(nextFeedStr, sizeof(nextFeedStr), "Tomorrow %02d:%02d", nextHour, nextMinute);
            } else {
                int nextDayOfWeek = (now.dayOfTheWeek() + daysAway) % 7;
                snprintf(nextFeedStr, sizeof(nextFeedStr), "%s %02d:%02d", dayNames[nextDayOfWeek], nextHour, nextMinute);
            }
            doc["nextFeed"] = nextFeedStr;
        } else {
            doc["nextFeed"] = nullptr;
        }
    } else {
        doc["nextFeed"] = nullptr;
    }

    // WiFi timeout info for heartbeat UI
    doc["wifiTimeoutSeconds"] = WIFI_IDLE_TIMEOUT_MS / 1000;
    doc["wifiRemainingSeconds"] = wifiManager.getRemainingIdleSeconds();

    String response;
    serializeJson(doc, response);
    sendJson(request, 200, response);
}

void WebServer::handleGetSchedules(AsyncWebServerRequest* request) {
    recordActivity();
    String json = storage.getSchedulesJson();
    sendJson(request, 200, json);
}

void WebServer::handleCreateSchedule(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    recordActivity();

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);

    if (error) {
        sendError(request, 400, "Invalid JSON");
        return;
    }

    Schedule schedule = {};
    strlcpy(schedule.name, doc["name"] | "", sizeof(schedule.name));
    schedule.hour = doc["hour"] | 6;
    schedule.minute = doc["minute"] | 0;
    schedule.days = doc["days"] | 0x7F;
    schedule.startMonth = doc["startMonth"] | -1;
    schedule.startDay = doc["startDay"] | -1;
    schedule.endMonth = doc["endMonth"] | -1;
    schedule.endDay = doc["endDay"] | -1;
    schedule.duration = doc["duration"] | 0;
    schedule.enabled = doc["enabled"] | true;

    if (storage.addSchedule(schedule)) {
        sendJson(request, 201, "{\"success\":true}");
    } else {
        sendError(request, 500, "Failed to add schedule");
    }
}

void WebServer::handleUpdateSchedule(AsyncWebServerRequest* request, uint8_t* data, size_t len, uint16_t id) {
    recordActivity();

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);

    if (error) {
        sendError(request, 400, "Invalid JSON");
        return;
    }

    Schedule* existing = storage.getScheduleById(id);
    if (!existing) {
        sendError(request, 404, "Schedule not found");
        return;
    }

    Schedule schedule = *existing;
    if (doc.containsKey("name")) strlcpy(schedule.name, doc["name"], sizeof(schedule.name));
    if (doc.containsKey("hour")) schedule.hour = doc["hour"];
    if (doc.containsKey("minute")) schedule.minute = doc["minute"];
    if (doc.containsKey("days")) schedule.days = doc["days"];
    if (doc.containsKey("startMonth")) schedule.startMonth = doc["startMonth"];
    if (doc.containsKey("startDay")) schedule.startDay = doc["startDay"];
    if (doc.containsKey("endMonth")) schedule.endMonth = doc["endMonth"];
    if (doc.containsKey("endDay")) schedule.endDay = doc["endDay"];
    if (doc.containsKey("duration")) schedule.duration = doc["duration"];
    if (doc.containsKey("enabled")) schedule.enabled = doc["enabled"];

    if (storage.updateSchedule(id, schedule)) {
        sendJson(request, 200, "{\"success\":true}");
    } else {
        sendError(request, 500, "Failed to update schedule");
    }
}

void WebServer::handleDeleteSchedule(AsyncWebServerRequest* request, uint16_t id) {
    recordActivity();

    if (storage.deleteSchedule(id)) {
        sendJson(request, 200, "{\"success\":true}");
    } else {
        sendError(request, 404, "Schedule not found");
    }
}

void WebServer::handleThrow(AsyncWebServerRequest* request) {
    recordActivity();

    if (motor.isRunning()) {
        sendError(request, 409, "Motor already running");
        return;
    }

    if (!battery.isMotorAllowed()) {
        sendError(request, 403, "Battery too low");
        return;
    }

    if (throwCallback) {
        throwCallback();
    }

    sendJson(request, 200, "{\"success\":true}");
}

void WebServer::handleTimeSync(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    recordActivity();

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);

    if (error) {
        sendError(request, 400, "Invalid JSON");
        return;
    }

    if (!doc.containsKey("epoch")) {
        sendError(request, 400, "Missing epoch field");
        return;
    }

    uint32_t epoch = doc["epoch"];

    // Apply timezone offset if provided (in minutes, like JS getTimezoneOffset())
    // getTimezoneOffset() returns positive for behind UTC (e.g., 360 for CST/UTC-6)
    if (doc.containsKey("offset")) {
        int32_t offsetMinutes = doc["offset"];
        epoch -= (offsetMinutes * 60);  // Convert UTC to local time
    }

    if (timeUpdateCallback) {
        timeUpdateCallback(epoch);
    }

    // Set time via RTC manager (also marks time as synced)
    rtcManager.setTime(epoch);

    Serial.printf("WebServer: Time synced to epoch %lu (local)\n", epoch);
    sendJson(request, 200, "{\"success\":true}");
}

void WebServer::handleGetSettings(AsyncWebServerRequest* request) {
    recordActivity();

    Settings& settings = storage.getSettings();

    JsonDocument doc;
    doc["deviceId"] = settings.deviceId;
    doc["motorDuration"] = settings.motorDuration;
    doc["vacationMode"] = settings.vacationMode;
    doc["batteryType"] = static_cast<uint8_t>(settings.batteryType);
    doc["batteryTypeName"] = settings.getBatteryTypeName();
    doc["batteryCriticalVoltage"] = settings.getCriticalVoltage();
    doc["sleepTimeout"] = static_cast<uint8_t>(settings.sleepTimeout);
    doc["sleepTimeoutName"] = settings.getSleepTimeoutName();
    doc["sleepTimeoutSeconds"] = settings.getSleepTimeoutSeconds();

    String response;
    serializeJson(doc, response);
    sendJson(request, 200, response);
}

void WebServer::handleUpdateSettings(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    recordActivity();

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);

    if (error) {
        sendError(request, 400, "Invalid JSON");
        return;
    }

    Settings& settings = storage.getSettings();

    if (doc["motorDuration"].is<uint8_t>()) {
        settings.motorDuration = doc["motorDuration"];
        motor.setDefaultDuration(settings.motorDuration);
    }
    if (doc["vacationMode"].is<bool>()) {
        settings.vacationMode = doc["vacationMode"];
    }
    if (doc["batteryType"].is<uint8_t>()) {
        uint8_t type = doc["batteryType"];
        if (type <= 2) {  // Valid range: 0=SLA, 1=AGM, 2=GEL
            settings.batteryType = static_cast<BatteryType>(type);
        }
    }
    if (doc["sleepTimeout"].is<uint8_t>()) {
        uint8_t timeout = doc["sleepTimeout"];
        // Validate against known enum values
        if (timeout == 0 || timeout == 15 || timeout == 30 ||
            timeout == 60 || timeout == 120 || timeout == 255) {
            settings.sleepTimeout = static_cast<SleepTimeout>(timeout);
        }
    }

    storage.saveSettings();
    sendJson(request, 200, "{\"success\":true}");
}

void WebServer::handleVacationMode(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    recordActivity();

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);

    if (error) {
        sendError(request, 400, "Invalid JSON");
        return;
    }

    if (!doc["enabled"].is<bool>()) {
        sendError(request, 400, "Missing enabled field");
        return;
    }

    storage.getSettings().vacationMode = doc["enabled"];
    storage.saveSettings();

    Serial.printf("WebServer: Vacation mode %s\n",
                  storage.getSettings().vacationMode ? "enabled" : "disabled");

    sendJson(request, 200, "{\"success\":true}");
}

void WebServer::sendJson(AsyncWebServerRequest* request, int code, const String& json) {
    request->send(code, "application/json", json);
}

void WebServer::sendError(AsyncWebServerRequest* request, int code, const char* message) {
    JsonDocument doc;
    doc["error"] = message;
    String response;
    serializeJson(doc, response);
    request->send(code, "application/json", response);
}
