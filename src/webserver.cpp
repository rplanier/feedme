#include "webserver.h"
#include "storage.h"
#include "motor.h"
#include "battery.h"
#include "wifi_manager.h"
#include "config.h"
#include "rtc_manager.h"
#include <Update.h>

FeedMeWebServer webServer;

void FeedMeWebServer::recordActivity() {
    lastActivityTime = millis();
    wifiManager.resetIdleTimer();  // Any API activity resets WiFi timeout
}

void FeedMeWebServer::begin() {
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

void FeedMeWebServer::stop() {
    if (!running) {
        return;
    }

    server->end();
    delete server;
    server = nullptr;
    running = false;

    Serial.println("WebServer: Stopped");
}

void FeedMeWebServer::setupRoutes() {
    setupStaticFiles();
    setupAPI();
    setupCaptivePortal();
}

void FeedMeWebServer::setupCaptivePortal() {
    // Captive portal response - serves a page that redirects to our app
    // This triggers iOS/Android captive portal popup
    const char* portalHTML =
        "<!DOCTYPE html><html><head>"
        "<meta http-equiv='refresh' content='0;url=http://192.168.4.1/'>"
        "<title>FeedMe Setup</title></head>"
        "<body><a href='http://192.168.4.1/'>Click here for FeedMe Setup</a></body></html>";

    // iOS captive portal detection URLs
    server->on("/hotspot-detect.html", HTTP_GET, [this, portalHTML](AsyncWebServerRequest* request) {
        recordActivity();
        request->send(200, "text/html", portalHTML);
    });

    server->on("/library/test/success.html", HTTP_GET, [this, portalHTML](AsyncWebServerRequest* request) {
        recordActivity();
        request->send(200, "text/html", portalHTML);
    });

    // Android captive portal detection
    server->on("/generate_204", HTTP_GET, [this, portalHTML](AsyncWebServerRequest* request) {
        recordActivity();
        request->send(200, "text/html", portalHTML);
    });

    server->on("/gen_204", HTTP_GET, [this, portalHTML](AsyncWebServerRequest* request) {
        recordActivity();
        request->send(200, "text/html", portalHTML);
    });

    // Windows captive portal detection
    server->on("/connecttest.txt", HTTP_GET, [this, portalHTML](AsyncWebServerRequest* request) {
        recordActivity();
        request->send(200, "text/html", portalHTML);
    });

    server->on("/ncsi.txt", HTTP_GET, [this, portalHTML](AsyncWebServerRequest* request) {
        recordActivity();
        request->send(200, "text/html", portalHTML);
    });

    // Catch-all for any other requests (other captive portal checks)
    server->onNotFound([this, portalHTML](AsyncWebServerRequest* request) {
        recordActivity();
        // Check if it's an API call that we missed
        String url = request->url();
        if (url.startsWith("/api/")) {
            request->send(404, "application/json", "{\"error\":\"Not found\"}");
        } else {
            request->send(200, "text/html", portalHTML);
        }
    });
}

void FeedMeWebServer::setupStaticFiles() {
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

void FeedMeWebServer::setupAPI() {
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
                    display.refreshIfBleSchedulesAffected();
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
                        display.refreshIfBleSchedulesAffected();
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
                    display.refreshIfBleSchedulesAffected();
                    sendJson(request, 201, "{\"success\":true}");
                } else {
                    sendError(request, 500, "Failed to add BLE schedule");
                }
            }
        }
    );

    // GET /api/feed-history - Get feed event history
    server->on("/api/feed-history", HTTP_GET, [this](AsyncWebServerRequest* request) {
        recordActivity();
        String json = storage.getFeedHistoryJson();
        sendJson(request, 200, json);
    });

    // POST /api/ota - Firmware update upload
    server->on("/api/ota", HTTP_POST,
        // Request handler (called when upload is complete)
        [this](AsyncWebServerRequest* request) {
            recordActivity();
            bool success = !Update.hasError();
            AsyncWebServerResponse* response = request->beginResponse(
                success ? 200 : 500,
                "application/json",
                success ? "{\"success\":true,\"message\":\"Update successful. Rebooting...\"}"
                        : "{\"success\":false,\"message\":\"Update failed\"}"
            );
            response->addHeader("Connection", "close");
            request->send(response);
            if (success) {
                delay(500);
                ESP.restart();
            }
        },
        // Upload handler (called for each chunk of data)
        [this](AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
            if (index == 0) {
                Serial.printf("OTA: Starting update with %s\n", filename.c_str());
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                    Update.printError(Serial);
                }
            }
            if (Update.write(data, len) != len) {
                Update.printError(Serial);
            }
            if (final) {
                if (Update.end(true)) {
                    Serial.printf("OTA: Update complete (%u bytes)\n", index + len);
                } else {
                    Update.printError(Serial);
                }
            }
        }
    );
}

void FeedMeWebServer::handleGetStatus(AsyncWebServerRequest* request) {
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
    doc["lastResetReason"] = getResetReasonString();

    // Current time from RTC manager (UTC with "Z" suffix so JS converts to local)
    DateTime now = rtcManager.now();
    char timeStr[32];
    snprintf(timeStr, sizeof(timeStr), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());
    doc["currentTime"] = timeStr;

    // Next feed time (only if time is synced and battery not critical)
    if (battery.getStatus() == BatteryStatus::CRITICAL) {
        doc["nextFeed"] = "Disabled - Low Battery";
    } else if (rtcManager.isTimeSynced()) {
        int nextHour, nextMinute, daysAway;
        if (storage.getNextRunTime(nextHour, nextMinute, daysAway)) {
            char nextFeedStr[32];
            static const char* dayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

            // Convert UTC hour to local time for display
            int16_t tzOffset = storage.getSettings().timezoneOffset;
            int localHour = nextHour - (tzOffset / 60);
            localHour = (localHour + 24) % 24;

            if (daysAway == 0) {
                snprintf(nextFeedStr, sizeof(nextFeedStr), "Today %02d:%02d", localHour, nextMinute);
            } else if (daysAway == 1) {
                snprintf(nextFeedStr, sizeof(nextFeedStr), "Tomorrow %02d:%02d", localHour, nextMinute);
            } else {
                int nextDayOfWeek = (now.dayOfTheWeek() + daysAway) % 7;
                snprintf(nextFeedStr, sizeof(nextFeedStr), "%s %02d:%02d", dayNames[nextDayOfWeek], localHour, nextMinute);
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

    // Diagnostics
    doc["uptime"] = millis() / 1000;
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["wifiClients"] = WiFi.softAPgetStationNum();
    doc["feedHistoryCount"] = storage.getFeedHistoryCount();

    String response;
    serializeJson(doc, response);
    sendJson(request, 200, response);
}

void FeedMeWebServer::handleGetSchedules(AsyncWebServerRequest* request) {
    recordActivity();
    String json = storage.getSchedulesJson();
    sendJson(request, 200, json);
}

void FeedMeWebServer::handleCreateSchedule(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
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
        display.refreshIfFeedSchedulesAffected();
        sendJson(request, 201, "{\"success\":true}");
    } else {
        sendError(request, 500, "Failed to add schedule");
    }
}

void FeedMeWebServer::handleUpdateSchedule(AsyncWebServerRequest* request, uint8_t* data, size_t len, uint16_t id) {
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
        display.refreshIfFeedSchedulesAffected();
        sendJson(request, 200, "{\"success\":true}");
    } else {
        sendError(request, 500, "Failed to update schedule");
    }
}

void FeedMeWebServer::handleDeleteSchedule(AsyncWebServerRequest* request, uint16_t id) {
    recordActivity();

    if (storage.deleteSchedule(id)) {
        display.refreshIfFeedSchedulesAffected();
        sendJson(request, 200, "{\"success\":true}");
    } else {
        sendError(request, 404, "Schedule not found");
    }
}

void FeedMeWebServer::handleThrow(AsyncWebServerRequest* request) {
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

    // Log manual feed event from web interface
    storage.logFeedEvent(motor.getDefaultDuration(), true, "");

    sendJson(request, 200, "{\"success\":true}");
}

void FeedMeWebServer::handleTimeSync(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
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

    // Store timezone offset if provided (in minutes, like JS getTimezoneOffset())
    // getTimezoneOffset() returns positive for behind UTC (e.g., 360 for CST/UTC-6)
    if (doc.containsKey("offset")) {
        int16_t offsetMinutes = doc["offset"];
        storage.getSettings().timezoneOffset = offsetMinutes;
        storage.saveSettings();
        Serial.printf("WebServer: Timezone offset set to %d minutes\n", offsetMinutes);
    }

    if (timeUpdateCallback) {
        timeUpdateCallback(epoch);
    }

    // Set time via RTC manager as UTC (also marks time as synced)
    rtcManager.setTime(epoch);

    display.refreshIfTimeAffected();
    Serial.printf("WebServer: Time synced to UTC epoch %lu\n", epoch);
    sendJson(request, 200, "{\"success\":true}");
}

void FeedMeWebServer::handleGetSettings(AsyncWebServerRequest* request) {
    recordActivity();

    Settings& settings = storage.getSettings();

    JsonDocument doc;
    doc["deviceId"] = settings.deviceId;
    doc["motorDuration"] = settings.motorDuration;
    doc["vacationMode"] = settings.vacationMode;
    doc["sleepTimeout"] = static_cast<uint8_t>(settings.sleepTimeout);
    doc["sleepTimeoutName"] = settings.getSleepTimeoutName();
    doc["sleepTimeoutSeconds"] = settings.getSleepTimeoutSeconds();
    doc["timezoneOffset"] = settings.timezoneOffset;
    doc["batteryType"] = static_cast<uint8_t>(settings.batteryType);

    // Human-readable battery type name
    switch (settings.batteryType) {
        case BatteryType::AGM: doc["batteryTypeName"] = "AGM"; break;
        case BatteryType::GEL: doc["batteryTypeName"] = "GEL"; break;
        case BatteryType::SLA:
        default: doc["batteryTypeName"] = "SLA"; break;
    }

    String response;
    serializeJson(doc, response);
    sendJson(request, 200, response);
}

void FeedMeWebServer::handleUpdateSettings(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
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
    if (doc["sleepTimeout"].is<uint8_t>()) {
        uint8_t timeout = doc["sleepTimeout"];
        // Validate against known enum values
        if (timeout == 0 || timeout == 15 || timeout == 30 ||
            timeout == 60 || timeout == 120 || timeout == 255) {
            settings.sleepTimeout = static_cast<SleepTimeout>(timeout);
        }
    }
    if (doc["batteryType"].is<uint8_t>()) {
        uint8_t type = doc["batteryType"];
        // Validate against known enum values (0=SLA, 1=AGM, 2=GEL)
        if (type <= 2) {
            settings.batteryType = static_cast<BatteryType>(type);
        }
    }

    storage.saveSettings();
    display.refreshIfSettingsAffected();
    sendJson(request, 200, "{\"success\":true}");
}

void FeedMeWebServer::handleVacationMode(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
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
    display.refreshIfSettingsAffected();

    Serial.printf("WebServer: Vacation mode %s\n",
                  storage.getSettings().vacationMode ? "enabled" : "disabled");

    sendJson(request, 200, "{\"success\":true}");
}

void FeedMeWebServer::sendJson(AsyncWebServerRequest* request, int code, const String& json) {
    request->send(code, "application/json", json);
}

void FeedMeWebServer::sendError(AsyncWebServerRequest* request, int code, const char* message) {
    JsonDocument doc;
    doc["error"] = message;
    String response;
    serializeJson(doc, response);
    request->send(code, "application/json", response);
}
