#include "display.h"
#include "storage.h"
#include "wifi_manager.h"
#include <time.h>
#include <sys/time.h>
#include "qrcode.h"

Display display;

void Display::begin() {
    tft.init();
    tft.setRotation(DISPLAY_ROTATION);
    tft.fillScreen(Colors::BACKGROUND);

    // Enable backlight
    pinMode(PIN_TFT_BACKLIGHT, OUTPUT);
    digitalWrite(PIN_TFT_BACKLIGHT, HIGH);

    // Initialize default status
    status.batteryVoltage = 0.0f;
    status.isCharging = false;
    status.batteryStatus = "---";
    strcpy(status.nextFeedTime, "--:--");
    strcpy(status.currentTime, "--:--");
    strcpy(status.currentDate, "---");
    status.wifiConnected = false;
    status.vacationMode = false;

    needsRedraw = true;
}

void Display::update() {
    // Auto-refresh overview screen when minute changes
    static int lastMinute = -1;
    if (currentScreen == Screen::OVERVIEW) {
        struct tm timeinfo;
        time_t now = time(nullptr);
        localtime_r(&now, &timeinfo);
        if (timeinfo.tm_min != lastMinute) {
            lastMinute = timeinfo.tm_min;
            needsRedraw = true;
        }
    }

    if (!needsRedraw) {
        return;
    }

    switch (currentScreen) {
        case Screen::OVERVIEW:
            drawOverviewScreen();
            break;
        case Screen::SCHEDULES:
            drawSchedulesScreen();
            break;
        case Screen::SCHEDULE_EDIT:
            drawScheduleEditScreen();
            break;
        case Screen::SETTINGS:
            drawSettingsScreen();
            break;
        case Screen::SETTINGS_EDIT:
            drawSettingsEditScreen();
            break;
        case Screen::TIME_EDIT:
            drawTimeEditScreen();
            break;
        case Screen::DATE_EDIT:
            drawDateEditScreen();
            break;
        case Screen::CONNECTIVITY:
            drawConnectivityScreen();
            break;
        case Screen::WIFI_SCHEDULES:
            drawWifiSchedulesScreen();
            break;
        case Screen::WIFI_SCHEDULE_EDIT:
            drawWifiScheduleEditScreen();
            break;
        case Screen::ABOUT:
            drawAboutScreen();
            break;
        case Screen::CONFIRM_RESET:
            drawConfirmResetScreen();
            break;
        case Screen::THROW_CONFIRM:
            drawThrowConfirmScreen();
            break;
    }

    needsRedraw = false;
}

void Display::handleButton(ButtonEvent event) {
    if (event == ButtonEvent::NONE) {
        return;
    }

    switch (currentScreen) {
        // Main screens cycle: OVERVIEW -> SCHEDULES -> CONNECTIVITY -> WIFI_SCHEDULES -> SETTINGS -> ABOUT -> OVERVIEW
        case Screen::OVERVIEW:
            if (event == ButtonEvent::TOP_PRESS) {
                setScreen(Screen::ABOUT);  // Previous (circular)
            } else if (event == ButtonEvent::BOTTOM_PRESS) {
                setScreen(Screen::SCHEDULES);  // Next
            }
            break;

        case Screen::SCHEDULES: {
            int scheduleCount = storage.getScheduleCount();
            if (!selectionMode) {
                if (event == ButtonEvent::TOP_PRESS) {
                    setScreen(Screen::OVERVIEW);
                } else if (event == ButtonEvent::BOTTOM_PRESS) {
                    setScreen(Screen::CONNECTIVITY);
                } else if (event == ButtonEvent::BOTTOM_HOLD) {
                    // Enter selection mode
                    selectionMode = true;
                    menuIndex = 0;
                    needsRedraw = true;
                }
            } else {
                // In selection mode
                if (event == ButtonEvent::TOP_PRESS) {
                    if (menuIndex > 0) menuIndex--;
                    needsRedraw = true;
                } else if (event == ButtonEvent::BOTTOM_PRESS) {
                    if (menuIndex < scheduleCount) menuIndex++;  // Max = scheduleCount (for "+ Add New")
                    needsRedraw = true;
                } else if (event == ButtonEvent::TOP_HOLD) {
                    // Exit selection mode
                    selectionMode = false;
                    needsRedraw = true;
                } else if (event == ButtonEvent::BOTTOM_HOLD) {
                    selectionMode = false;
                    Schedule* sched = nullptr;
                    if (menuIndex < scheduleCount) {
                        // Edit existing schedule
                        editScheduleIndex = menuIndex;
                        sched = storage.getSchedule(menuIndex);
                    } else {
                        // Add new schedule - create default and edit
                        Schedule newSched = {};
                        newSched.hour = 7;
                        newSched.minute = 0;
                        newSched.days = 0x7F;  // All days
                        newSched.enabled = true;
                        storage.addSchedule(newSched);
                        editScheduleIndex = storage.getScheduleCount() - 1;
                        sched = storage.getSchedule(editScheduleIndex);
                    }
                    // Initialize edit state from schedule
                    if (sched) {
                        editHour = sched->hour;
                        editMinute = sched->minute;
                        editDays = sched->days;
                        editDuration = sched->duration;
                        editEnabled = sched->enabled;
                        editField = 0;
                        editSubField = 0;
                        setScreen(Screen::SCHEDULE_EDIT);
                    }
                }
            }
            break;
        }

        case Screen::SCHEDULE_EDIT:
            // Fields: 0=Time, 1=Days, 2=Duration, 3=Enabled, 4=Delete
            // Two-level state: editingField = actively editing a field's value
            // Press ALWAYS moves caret, Hold enters/exits edit mode (consistent with Settings)
            if (!editingField) {
                // Navigating between fields
                if (event == ButtonEvent::TOP_HOLD) {
                    // Save and exit
                    Schedule* sched = storage.getSchedule(editScheduleIndex);
                    if (sched) {
                        sched->hour = editHour;
                        sched->minute = editMinute;
                        sched->days = editDays;
                        sched->duration = editDuration;
                        sched->enabled = editEnabled;
                        sched->generateName();
                        storage.saveSchedules();
                    }
                    setScreen(Screen::SCHEDULES);
                } else if (event == ButtonEvent::TOP_PRESS) {
                    // Move caret up
                    if (editField > 0) editField--;
                    needsRedraw = true;
                } else if (event == ButtonEvent::BOTTOM_PRESS) {
                    // Move caret down
                    if (editField < 4) editField++;
                    needsRedraw = true;
                } else if (event == ButtonEvent::BOTTOM_HOLD) {
                    // Enter edit mode or perform action
                    switch (editField) {
                        case 0: // Time - enter edit mode
                        case 2: // Duration - enter edit mode
                            editingField = true;
                            editSubField = 0;
                            break;
                        case 1: // Days - cycle directly (simple toggle)
                            if (editDays == 0x7F) editDays = 0x3E;
                            else if (editDays == 0x3E) editDays = 0x41;
                            else if (editDays == 0x41) editDays = 0x7F;
                            else editDays = 0x7F;
                            break;
                        case 3: // Enabled - toggle directly
                            editEnabled = !editEnabled;
                            break;
                        case 4: // Delete - perform action
                            {
                                Schedule* sched = storage.getSchedule(editScheduleIndex);
                                if (sched) {
                                    storage.deleteSchedule(sched->id);
                                }
                                setScreen(Screen::SCHEDULES);
                            }
                            return;
                    }
                    needsRedraw = true;
                }
            } else {
                // Actively editing a field's value
                if (event == ButtonEvent::TOP_PRESS) {
                    // Decrease value
                    if (editField == 0) {
                        if (editSubField == 0) editHour = (editHour + 23) % 24;
                        else editMinute = (editMinute + 59) % 60;
                    } else if (editField == 2) {
                        if (editDuration > 0) editDuration--;
                    }
                    needsRedraw = true;
                } else if (event == ButtonEvent::BOTTOM_PRESS) {
                    // Increase value
                    if (editField == 0) {
                        if (editSubField == 0) editHour = (editHour + 1) % 24;
                        else editMinute = (editMinute + 1) % 60;
                    } else if (editField == 2) {
                        if (editDuration < 30) editDuration++;
                    }
                    needsRedraw = true;
                } else if (event == ButtonEvent::BOTTOM_HOLD) {
                    // Advance sub-field or exit edit mode
                    if (editField == 0) {
                        // Time: advance hour->minute or exit
                        if (editSubField == 0) editSubField = 1;
                        else editingField = false;
                    } else if (editField == 2) {
                        // Duration has no sub-fields, exit edit mode
                        editingField = false;
                    }
                    needsRedraw = true;
                } else if (event == ButtonEvent::TOP_HOLD) {
                    // Exit edit mode
                    editingField = false;
                    needsRedraw = true;
                }
            }
            break;

        case Screen::SETTINGS:
            // Fields: 0=Motor, 1=Vacation, 2=Time, 3=Date, 4=Reset
            // Two-level state: selectionMode = in settings, editingField = actively editing a field
            // Press ALWAYS moves caret, Hold enters/exits edit mode
            if (!selectionMode) {
                if (event == ButtonEvent::TOP_PRESS) {
                    setScreen(Screen::WIFI_SCHEDULES);
                } else if (event == ButtonEvent::BOTTOM_PRESS) {
                    setScreen(Screen::ABOUT);
                } else if (event == ButtonEvent::BOTTOM_HOLD) {
                    // Enter selection mode, initialize time/date from RTC
                    selectionMode = true;
                    editingField = false;
                    menuIndex = 0;
                    editSubField = 0;
                    editValue = storage.getSettings().motorDuration;
                    struct tm timeinfo;
                    time_t now = time(nullptr);
                    localtime_r(&now, &timeinfo);
                    editHour = timeinfo.tm_hour;
                    editMinute = timeinfo.tm_min;
                    editMonth = timeinfo.tm_mon + 1;
                    editDay = timeinfo.tm_mday;
                    editYear = timeinfo.tm_year + 1900;
                    if (editMonth < 1 || editMonth > 12) editMonth = 1;
                    if (editDay < 1 || editDay > 31) editDay = 1;
                    if (editYear < 2024 || editYear > 2035) editYear = 2025;
                    needsRedraw = true;
                }
            } else if (!editingField) {
                // Selection mode, navigating between fields
                if (event == ButtonEvent::TOP_HOLD) {
                    // Save and exit selection mode
                    storage.getSettings().motorDuration = editValue;
                    storage.getSettings().vacationMode = status.vacationMode;
                    storage.saveSettings();
                    // Save time to RTC
                    struct tm timeinfo;
                    time_t now = time(nullptr);
                    localtime_r(&now, &timeinfo);
                    timeinfo.tm_hour = editHour;
                    timeinfo.tm_min = editMinute;
                    timeinfo.tm_sec = 0;
                    timeinfo.tm_year = editYear - 1900;
                    timeinfo.tm_mon = editMonth - 1;
                    timeinfo.tm_mday = editDay;
                    struct timeval tv;
                    tv.tv_sec = mktime(&timeinfo);
                    tv.tv_usec = 0;
                    settimeofday(&tv, nullptr);
                    selectionMode = false;
                    editingField = false;
                    needsRedraw = true;
                } else if (event == ButtonEvent::TOP_PRESS) {
                    // Move caret up
                    if (menuIndex > 0) menuIndex--;
                    needsRedraw = true;
                } else if (event == ButtonEvent::BOTTOM_PRESS) {
                    // Move caret down
                    if (menuIndex < 4) menuIndex++;
                    needsRedraw = true;
                } else if (event == ButtonEvent::BOTTOM_HOLD) {
                    // Enter edit mode or perform action
                    switch (menuIndex) {
                        case 0: // Motor - enter edit mode
                        case 2: // Time - enter edit mode
                        case 3: // Date - enter edit mode
                            editingField = true;
                            editSubField = 0;
                            break;
                        case 1: // Vacation - toggle directly (no edit mode needed)
                            status.vacationMode = !status.vacationMode;
                            break;
                        case 4: // Reset - go to confirm screen
                            setScreen(Screen::CONFIRM_RESET);
                            return;
                    }
                    needsRedraw = true;
                }
            } else {
                // Actively editing a field's value
                if (event == ButtonEvent::TOP_PRESS) {
                    // Decrease value
                    if (menuIndex == 0) {
                        if (editValue > 1) editValue--;
                    } else if (menuIndex == 2) {
                        if (editSubField == 0) editHour = (editHour + 23) % 24;
                        else editMinute = (editMinute + 59) % 60;
                    } else if (menuIndex == 3) {
                        if (editSubField == 0) editMonth = (editMonth <= 1) ? 12 : editMonth - 1;
                        else if (editSubField == 1) editDay = (editDay <= 1) ? 31 : editDay - 1;
                        else editYear = (editYear <= 2024) ? 2035 : editYear - 1;
                    }
                    needsRedraw = true;
                } else if (event == ButtonEvent::BOTTOM_PRESS) {
                    // Increase value
                    if (menuIndex == 0) {
                        if (editValue < MOTOR_MAX_DURATION_SEC) editValue++;
                    } else if (menuIndex == 2) {
                        if (editSubField == 0) editHour = (editHour + 1) % 24;
                        else editMinute = (editMinute + 1) % 60;
                    } else if (menuIndex == 3) {
                        if (editSubField == 0) editMonth = (editMonth >= 12) ? 1 : editMonth + 1;
                        else if (editSubField == 1) editDay = (editDay >= 31) ? 1 : editDay + 1;
                        else editYear = (editYear >= 2035) ? 2024 : editYear + 1;
                    }
                    needsRedraw = true;
                } else if (event == ButtonEvent::BOTTOM_HOLD) {
                    // Advance sub-field or exit edit mode
                    if (menuIndex == 0) {
                        // Motor has no sub-fields, exit edit mode
                        editingField = false;
                    } else if (menuIndex == 2) {
                        // Time: advance hour->minute or exit
                        if (editSubField == 0) editSubField = 1;
                        else editingField = false;
                    } else if (menuIndex == 3) {
                        // Date: advance month->day->year or exit
                        if (editSubField < 2) editSubField++;
                        else editingField = false;
                    }
                    needsRedraw = true;
                } else if (event == ButtonEvent::TOP_HOLD) {
                    // Exit edit mode (cancel current field edit)
                    editingField = false;
                    needsRedraw = true;
                }
            }
            break;

        case Screen::SETTINGS_EDIT:
            if (event == ButtonEvent::TOP_HOLD) {
                // Cancel and go back
                setScreen(Screen::SETTINGS);
            } else if (event == ButtonEvent::TOP_PRESS) {
                if (editValue > 1) editValue--;
                needsRedraw = true;
            } else if (event == ButtonEvent::BOTTOM_PRESS) {
                if (editValue < MOTOR_MAX_DURATION_SEC) editValue++;
                needsRedraw = true;
            } else if (event == ButtonEvent::BOTTOM_HOLD) {
                // Save motor duration
                storage.getSettings().motorDuration = editValue;
                storage.saveSettings();
                setScreen(Screen::SETTINGS);
            }
            break;

        case Screen::TIME_EDIT:
            if (event == ButtonEvent::TOP_HOLD) {
                setScreen(Screen::SETTINGS);
            } else if (event == ButtonEvent::TOP_PRESS) {
                // Decrease current field
                if (editField == 0) {
                    editHour = (editHour + 23) % 24;  // Wrap 0->23
                } else {
                    editMinute = (editMinute + 59) % 60;  // Wrap 0->59
                }
                needsRedraw = true;
            } else if (event == ButtonEvent::BOTTOM_PRESS) {
                // Increase current field
                if (editField == 0) {
                    editHour = (editHour + 1) % 24;
                } else {
                    editMinute = (editMinute + 1) % 60;
                }
                needsRedraw = true;
            } else if (event == ButtonEvent::BOTTOM_HOLD) {
                if (editField == 0) {
                    // Move to minute field
                    editField = 1;
                    needsRedraw = true;
                } else {
                    // Save time to RTC
                    struct tm timeinfo;
                    time_t now = time(nullptr);
                    localtime_r(&now, &timeinfo);
                    timeinfo.tm_hour = editHour;
                    timeinfo.tm_min = editMinute;
                    timeinfo.tm_sec = 0;

                    struct timeval tv;
                    tv.tv_sec = mktime(&timeinfo);
                    tv.tv_usec = 0;
                    settimeofday(&tv, nullptr);

                    snprintf(status.currentTime, sizeof(status.currentTime), "%02d:%02d", editHour, editMinute);
                    setScreen(Screen::SETTINGS);
                }
            }
            break;

        case Screen::DATE_EDIT:
            if (event == ButtonEvent::TOP_HOLD) {
                setScreen(Screen::SETTINGS);
            } else if (event == ButtonEvent::TOP_PRESS) {
                // Decrease current field
                if (editField == 0) {
                    editMonth = (editMonth <= 1) ? 12 : editMonth - 1;
                } else if (editField == 1) {
                    editDay = (editDay <= 1) ? 31 : editDay - 1;
                } else {
                    editYear = (editYear <= 2024) ? 2035 : editYear - 1;
                }
                needsRedraw = true;
            } else if (event == ButtonEvent::BOTTOM_PRESS) {
                // Increase current field
                if (editField == 0) {
                    editMonth = (editMonth >= 12) ? 1 : editMonth + 1;
                } else if (editField == 1) {
                    editDay = (editDay >= 31) ? 1 : editDay + 1;
                } else {
                    editYear = (editYear >= 2035) ? 2024 : editYear + 1;
                }
                needsRedraw = true;
            } else if (event == ButtonEvent::BOTTOM_HOLD) {
                if (editField < 2) {
                    editField++;
                    needsRedraw = true;
                } else {
                    // Save date to RTC
                    static const char* monthNames[] = {"", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

                    struct tm timeinfo;
                    time_t now = time(nullptr);
                    localtime_r(&now, &timeinfo);
                    timeinfo.tm_year = editYear - 1900;
                    timeinfo.tm_mon = editMonth - 1;
                    timeinfo.tm_mday = editDay;

                    struct timeval tv;
                    tv.tv_sec = mktime(&timeinfo);
                    tv.tv_usec = 0;
                    settimeofday(&tv, nullptr);

                    snprintf(status.currentDate, sizeof(status.currentDate), "%s %d",
                             monthNames[editMonth], editDay);
                    setScreen(Screen::SETTINGS);
                }
            }
            break;

        case Screen::CONNECTIVITY:
            if (event == ButtonEvent::TOP_PRESS) {
                setScreen(Screen::SCHEDULES);
            } else if (event == ButtonEvent::BOTTOM_PRESS) {
                setScreen(Screen::WIFI_SCHEDULES);
            } else if (event == ButtonEvent::BOTTOM_HOLD) {
                // Toggle WiFi
                if (wifiManager.isRunning()) {
                    wifiManager.stop();
                } else {
                    wifiManager.start();
                }
                needsRedraw = true;
            }
            break;

        case Screen::WIFI_SCHEDULES: {
            int wifiScheduleCount = storage.getWifiScheduleCount();
            if (!selectionMode) {
                if (event == ButtonEvent::TOP_PRESS) {
                    setScreen(Screen::CONNECTIVITY);
                } else if (event == ButtonEvent::BOTTOM_PRESS) {
                    setScreen(Screen::SETTINGS);
                } else if (event == ButtonEvent::BOTTOM_HOLD) {
                    // Enter selection mode
                    selectionMode = true;
                    menuIndex = 0;
                    needsRedraw = true;
                }
            } else {
                // In selection mode
                if (event == ButtonEvent::TOP_PRESS) {
                    if (menuIndex > 0) menuIndex--;
                    needsRedraw = true;
                } else if (event == ButtonEvent::BOTTOM_PRESS) {
                    if (menuIndex < wifiScheduleCount) menuIndex++;  // Max = count (for "+ Add New")
                    needsRedraw = true;
                } else if (event == ButtonEvent::TOP_HOLD) {
                    // Exit selection mode
                    selectionMode = false;
                    needsRedraw = true;
                } else if (event == ButtonEvent::BOTTOM_HOLD) {
                    selectionMode = false;
                    WifiSchedule* sched = nullptr;
                    if (menuIndex < wifiScheduleCount) {
                        // Edit existing schedule
                        editWifiScheduleIndex = menuIndex;
                        sched = storage.getWifiSchedule(menuIndex);
                    } else {
                        // Add new schedule - create default and edit
                        WifiSchedule newSched = {};
                        newSched.startHour = 6;
                        newSched.startMinute = 0;
                        newSched.endHour = 8;
                        newSched.endMinute = 0;
                        newSched.days = 0x7F;  // All days
                        newSched.enabled = true;
                        strlcpy(newSched.name, "WiFi Window", sizeof(newSched.name));
                        storage.addWifiSchedule(newSched);
                        editWifiScheduleIndex = storage.getWifiScheduleCount() - 1;
                        sched = storage.getWifiSchedule(editWifiScheduleIndex);
                    }
                    // Initialize edit state from schedule
                    if (sched) {
                        editStartHour = sched->startHour;
                        editStartMinute = sched->startMinute;
                        editEndHour = sched->endHour;
                        editEndMinute = sched->endMinute;
                        editDays = sched->days;
                        editEnabled = sched->enabled;
                        editField = 0;
                        editSubField = 0;
                        setScreen(Screen::WIFI_SCHEDULE_EDIT);
                    }
                }
            }
            break;
        }

        case Screen::WIFI_SCHEDULE_EDIT:
            // Fields: 0=StartTime, 1=EndTime, 2=Days, 3=Enabled, 4=Delete
            if (!editingField) {
                // Navigating between fields
                if (event == ButtonEvent::TOP_HOLD) {
                    // Save and exit
                    WifiSchedule* sched = storage.getWifiSchedule(editWifiScheduleIndex);
                    if (sched) {
                        sched->startHour = editStartHour;
                        sched->startMinute = editStartMinute;
                        sched->endHour = editEndHour;
                        sched->endMinute = editEndMinute;
                        sched->days = editDays;
                        sched->enabled = editEnabled;
                        storage.saveWifiSchedules();
                    }
                    setScreen(Screen::WIFI_SCHEDULES);
                } else if (event == ButtonEvent::TOP_PRESS) {
                    if (editField > 0) editField--;
                    needsRedraw = true;
                } else if (event == ButtonEvent::BOTTOM_PRESS) {
                    if (editField < 4) editField++;
                    needsRedraw = true;
                } else if (event == ButtonEvent::BOTTOM_HOLD) {
                    switch (editField) {
                        case 0: // Start Time - enter edit mode
                        case 1: // End Time - enter edit mode
                            editingField = true;
                            editSubField = 0;
                            break;
                        case 2: // Days - cycle directly
                            if (editDays == 0x7F) editDays = 0x3E;
                            else if (editDays == 0x3E) editDays = 0x41;
                            else if (editDays == 0x41) editDays = 0x7F;
                            else editDays = 0x7F;
                            break;
                        case 3: // Enabled - toggle directly
                            editEnabled = !editEnabled;
                            break;
                        case 4: // Delete - perform action
                            {
                                WifiSchedule* sched = storage.getWifiSchedule(editWifiScheduleIndex);
                                if (sched) {
                                    storage.deleteWifiSchedule(sched->id);
                                }
                                setScreen(Screen::WIFI_SCHEDULES);
                            }
                            return;
                    }
                    needsRedraw = true;
                }
            } else {
                // Actively editing start/end time
                if (event == ButtonEvent::TOP_PRESS) {
                    // Decrease value
                    if (editField == 0) {
                        if (editSubField == 0) editStartHour = (editStartHour + 23) % 24;
                        else editStartMinute = (editStartMinute + 59) % 60;
                    } else if (editField == 1) {
                        if (editSubField == 0) editEndHour = (editEndHour + 23) % 24;
                        else editEndMinute = (editEndMinute + 59) % 60;
                    }
                    needsRedraw = true;
                } else if (event == ButtonEvent::BOTTOM_PRESS) {
                    // Increase value
                    if (editField == 0) {
                        if (editSubField == 0) editStartHour = (editStartHour + 1) % 24;
                        else editStartMinute = (editStartMinute + 1) % 60;
                    } else if (editField == 1) {
                        if (editSubField == 0) editEndHour = (editEndHour + 1) % 24;
                        else editEndMinute = (editEndMinute + 1) % 60;
                    }
                    needsRedraw = true;
                } else if (event == ButtonEvent::BOTTOM_HOLD) {
                    // Advance sub-field or exit edit mode
                    if (editSubField == 0) editSubField = 1;
                    else editingField = false;
                    needsRedraw = true;
                } else if (event == ButtonEvent::TOP_HOLD) {
                    // Exit edit mode
                    editingField = false;
                    needsRedraw = true;
                }
            }
            break;

        case Screen::ABOUT:
            if (event == ButtonEvent::TOP_PRESS) {
                setScreen(Screen::SETTINGS);
            } else if (event == ButtonEvent::BOTTOM_PRESS) {
                setScreen(Screen::OVERVIEW);  // Circular back to start
            }
            break;

        case Screen::CONFIRM_RESET:
            if (event == ButtonEvent::TOP_PRESS || event == ButtonEvent::TOP_HOLD) {
                setScreen(Screen::SETTINGS);  // Cancel
            } else if (event == ButtonEvent::BOTTOM_HOLD) {
                // Perform reset
                storage.resetToDefaults();
                setScreen(Screen::OVERVIEW);
            }
            break;

        case Screen::THROW_CONFIRM:
            if (event == ButtonEvent::TOP_PRESS || event == ButtonEvent::TOP_HOLD) {
                setScreen(previousScreen);
            } else if (event == ButtonEvent::BOTTOM_HOLD) {
                // TODO: trigger motor
                setScreen(previousScreen);
            }
            break;
    }
}

void Display::setScreen(Screen screen) {
    if (screen != currentScreen) {
        previousScreen = currentScreen;
        currentScreen = screen;
        menuIndex = 0;
        scrollOffset = 0;
        selectionMode = false;
        editingField = false;
        needsRedraw = true;
    }
}

void Display::setStatus(const StatusData& newStatus) {
    status = newStatus;
    if (currentScreen == Screen::OVERVIEW) {
        needsRedraw = true;
    }
}

void Display::setPairingInfo(const char* ssid, const char* password) {
    strncpy(pairingSSID, ssid, sizeof(pairingSSID) - 1);
    strncpy(pairingPassword, password, sizeof(pairingPassword) - 1);
    if (currentScreen == Screen::CONNECTIVITY) {
        needsRedraw = true;
    }
}

void Display::drawHeader(const char* title) {
    tft.fillRect(0, 0, SCREEN_WIDTH, 24, Colors::HEADER_BG);
    tft.setTextColor(Colors::TEXT, Colors::HEADER_BG);
    tft.setTextSize(2);
    tft.setCursor(8, 4);
    tft.print(title);

    // Draw connection status icons on right side
    drawConnectionStatus(SCREEN_WIDTH - 50, 4);
}

void Display::clearContent() {
    tft.fillRect(0, 24, SCREEN_WIDTH, SCREEN_HEIGHT - 24, Colors::BACKGROUND);
}

void Display::drawOverviewScreen() {
    tft.fillScreen(Colors::BACKGROUND);
    char headerTitle[32];
    snprintf(headerTitle, sizeof(headerTitle), "FeedMe v%s", FEEDME_VERSION);
    drawHeader(headerTitle);

    // Get current time/date fresh from RTC
    struct tm timeinfo;
    time_t now = time(nullptr);
    localtime_r(&now, &timeinfo);

    static const char* monthNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char displayTime[6];
    char displayDate[16];
    snprintf(displayTime, sizeof(displayTime), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    if (timeinfo.tm_year > 100) {  // Valid year (after 2000)
        snprintf(displayDate, sizeof(displayDate), "%s %d", monthNames[timeinfo.tm_mon], timeinfo.tm_mday);
    } else {
        strcpy(displayDate, "---");
    }

    // Current time - large display
    tft.setTextColor(Colors::TEXT, Colors::BACKGROUND);
    tft.setTextSize(4);
    tft.setCursor(20, 45);
    tft.print(displayTime);

    // Current date
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(160, 55);
    tft.print(displayDate);
    tft.setTextFont(1);

    // Calculate next run time fresh
    char nextRunText[20];
    int nextHour, nextMinute, daysAway;
    if (storage.getNextRunTime(nextHour, nextMinute, daysAway)) {
        static const char* dayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        int nextDayOfWeek = (timeinfo.tm_wday + daysAway) % 7;

        if (daysAway == 0) {
            snprintf(nextRunText, sizeof(nextRunText), "Today %02d:%02d", nextHour, nextMinute);
        } else if (daysAway == 1) {
            snprintf(nextRunText, sizeof(nextRunText), "Tomorrow %02d:%02d", nextHour, nextMinute);
        } else {
            snprintf(nextRunText, sizeof(nextRunText), "%s %02d:%02d", dayNames[nextDayOfWeek], nextHour, nextMinute);
        }
    } else {
        strcpy(nextRunText, "--:--");
    }

    // Next feed time - use Font 2 for medium size
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(20, 85);
    tft.print("Next Run: ");
    tft.setTextColor(Colors::ACCENT, Colors::BACKGROUND);
    tft.print(nextRunText);

    // Battery status
    drawBatteryIndicator(20, 110);

    // Reset to default font
    tft.setTextFont(1);

    // Vacation mode indicator (read from storage, not cached status)
    if (storage.getSettings().vacationMode) {
        tft.setTextFont(2);
        tft.setTextSize(1);
        tft.setTextColor(Colors::WARNING, Colors::BACKGROUND);
        tft.setCursor(20, 145);
        tft.print("VACATION MODE");
        tft.setTextFont(1);
    }

    drawNavCarets();
}

void Display::drawBatteryIndicator(int x, int y) {
    uint16_t color = getBatteryColor();

    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(x, y);
    tft.print("Battery: ");

    tft.setTextColor(color, Colors::BACKGROUND);
    tft.print(status.batteryStatus);

    // Voltage
    if (status.batteryVoltage > 0) {
        tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
        tft.printf(" %.1fV", status.batteryVoltage);
    }

    // Charging indicator
    if (status.isCharging) {
        tft.setTextColor(Colors::SUCCESS, Colors::BACKGROUND);
        tft.print(" CHG");
    }

    tft.setTextFont(1);
}

void Display::drawConnectionStatus(int x, int y) {
    tft.setTextSize(1);

    // WiFi indicator
    if (wifiManager.isRunning()) {
        tft.setTextColor(Colors::SUCCESS, Colors::HEADER_BG);
        tft.setCursor(x, y + 4);
        tft.print("WiFi");
    }
}

uint16_t Display::getBatteryColor() const {
    if (status.batteryVoltage >= BATTERY_VOLTAGE_GOOD) {
        return Colors::SUCCESS;
    } else if (status.batteryVoltage >= BATTERY_VOLTAGE_OKAY) {
        return Colors::WARNING;
    } else {
        return Colors::DANGER;
    }
}

void Display::drawNavCarets() {
    tft.setTextFont(1);
    tft.setTextSize(2);
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(300, 30);
    tft.print("<");
    tft.setCursor(300, 140);
    tft.print(">");
}

void Display::drawMenuItem(int y, const char* label, const char* value, bool selected) {
    tft.setTextFont(2);
    tft.setTextSize(1);

    // Selection caret
    if (selected) {
        tft.setTextColor(Colors::ACCENT, Colors::BACKGROUND);
        tft.setCursor(5, y);
        tft.print(">");
    }

    // Label
    tft.setTextColor(Colors::TEXT, Colors::BACKGROUND);
    tft.setCursor(20, y);
    tft.print(label);

    // Value (if provided)
    if (value && value[0]) {
        tft.setTextColor(Colors::ACCENT, Colors::BACKGROUND);
        tft.setCursor(160, y);
        tft.print(value);
    }

    tft.setTextFont(1);
}

void Display::drawSchedulesScreen() {
    tft.fillScreen(Colors::BACKGROUND);
    drawHeader("Feed Schedules");

    tft.setTextFont(2);
    tft.setTextSize(1);

    int y = 35;
    int scheduleCount = storage.getScheduleCount();
    char valueBuf[16];

    // Show schedules from storage
    for (int i = 0; i < scheduleCount && i < 4; i++) {
        Schedule* s = storage.getSchedule(i);
        if (s) {
            // Show duration or "off" if disabled
            if (s->enabled) {
                uint8_t dur = s->duration > 0 ? s->duration : storage.getSettings().motorDuration;
                snprintf(valueBuf, sizeof(valueBuf), "%ds", dur);
            } else {
                snprintf(valueBuf, sizeof(valueBuf), "off");
            }
            drawMenuItem(y, s->name, valueBuf, selectionMode && menuIndex == i);
            y += 22;
        }
    }

    // Add New option
    drawMenuItem(y, "+ Add New", "", selectionMode && menuIndex == scheduleCount);

    // Instructions
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(10, 150);
    if (!selectionMode) {
        tft.print("[Hold] Edit");
    } else {
        tft.print("[Hold] Select  [Hold Top] Back");
    }

    tft.setTextFont(1);
    drawNavCarets();
}

void Display::drawScheduleEditScreen() {
    tft.fillScreen(Colors::BACKGROUND);
    drawHeader("Edit Feed Sched");

    tft.setTextFont(2);
    tft.setTextSize(1);

    int y = 32;
    const int lineHeight = 22;
    const int labelX = 20;
    const int valueX = 110;

    // Time (combined hour:minute)
    bool timeSelected = (editField == 0);
    bool timeEditing = timeSelected && editingField;
    bool editingHour = timeEditing && editSubField == 0;
    bool editingMin = timeEditing && editSubField == 1;
    tft.setTextColor(timeSelected ? Colors::ACCENT : Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(5, y);
    tft.print(timeSelected ? ">" : " ");
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(labelX, y);
    tft.print("Time:");
    tft.setTextColor(editingHour ? Colors::ACCENT : Colors::TEXT, Colors::BACKGROUND);
    tft.setCursor(valueX, y);
    tft.printf("%02d", editHour);
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.print(":");
    tft.setTextColor(editingMin ? Colors::ACCENT : Colors::TEXT, Colors::BACKGROUND);
    tft.printf("%02d", editMinute);
    y += lineHeight;

    // Days
    bool daysSelected = (editField == 1);
    tft.setTextColor(daysSelected ? Colors::ACCENT : Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(5, y);
    tft.print(daysSelected ? ">" : " ");
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(labelX, y);
    tft.print("Days:");
    tft.setTextColor(Colors::TEXT, Colors::BACKGROUND);
    tft.setCursor(valueX, y);
    if (editDays == 0x7F) tft.print("Daily");
    else if (editDays == 0x3E) tft.print("Weekdays");
    else if (editDays == 0x41) tft.print("Weekends");
    else tft.print("Custom");
    y += lineHeight;

    // Duration
    bool durSelected = (editField == 2);
    bool durEditing = durSelected && editingField;
    tft.setTextColor(durSelected ? Colors::ACCENT : Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(5, y);
    tft.print(durSelected ? ">" : " ");
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(labelX, y);
    tft.print("Duration:");
    tft.setTextColor(durEditing ? Colors::ACCENT : Colors::TEXT, Colors::BACKGROUND);
    tft.setCursor(valueX, y);
    if (editDuration == 0) {
        tft.printf("Default (%ds)", storage.getSettings().motorDuration);
    } else {
        tft.printf("%d sec", editDuration);
    }
    y += lineHeight;

    // Enabled
    bool enabledSelected = (editField == 3);
    tft.setTextColor(enabledSelected ? Colors::ACCENT : Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(5, y);
    tft.print(enabledSelected ? ">" : " ");
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(labelX, y);
    tft.print("Enabled:");
    tft.setTextColor(editEnabled ? Colors::SUCCESS : Colors::DANGER, Colors::BACKGROUND);
    tft.setCursor(valueX, y);
    tft.print(editEnabled ? "ON" : "OFF");
    y += lineHeight;

    // Delete
    bool deleteSelected = (editField == 4);
    tft.setTextColor(deleteSelected ? Colors::DANGER : Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(5, y);
    tft.print(deleteSelected ? ">" : " ");
    tft.setCursor(labelX, y);
    tft.print("Delete");

    // Instructions
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(10, 150);
    if (editingField) {
        tft.print("[Press] Adjust  [Hold] Next");
    } else if (editField == 4) {
        tft.print("[Hold] Delete  [Hold Top] Save");
    } else if (editField == 1 || editField == 3) {
        tft.print("[Hold] Toggle  [Hold Top] Save");
    } else {
        tft.print("[Hold] Edit    [Hold Top] Save");
    }

    tft.setTextFont(1);
}

void Display::drawSettingsScreen() {
    tft.fillScreen(Colors::BACKGROUND);
    drawHeader("Settings");

    tft.setTextFont(2);
    tft.setTextSize(1);

    int y = 32;
    const int lineHeight = 22;
    const int labelX = 20;
    const int valueX = 110;

    static const char* monthNames[] = {"", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    // Get fresh time/date from RTC for display
    struct tm timeinfo;
    time_t now = time(nullptr);
    localtime_r(&now, &timeinfo);
    char displayTime[6];
    char displayDate[16];
    snprintf(displayTime, sizeof(displayTime), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    snprintf(displayDate, sizeof(displayDate), "%s %02d %d",
             monthNames[timeinfo.tm_mon + 1], timeinfo.tm_mday, timeinfo.tm_year + 1900);

    // Motor duration
    bool motorSelected = selectionMode && menuIndex == 0;
    bool motorEditing = motorSelected && editingField;
    tft.setTextColor(motorSelected ? Colors::ACCENT : Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(5, y);
    tft.print(motorSelected ? ">" : " ");
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(labelX, y);
    tft.print("Motor:");
    tft.setTextColor(motorEditing ? Colors::ACCENT : Colors::TEXT, Colors::BACKGROUND);
    tft.setCursor(valueX, y);
    tft.printf("%d sec", selectionMode ? editValue : storage.getSettings().motorDuration);
    y += lineHeight;

    // Vacation mode
    bool vacMode = selectionMode ? status.vacationMode : storage.getSettings().vacationMode;
    bool vacSelected = selectionMode && menuIndex == 1;
    tft.setTextColor(vacSelected ? Colors::ACCENT : Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(5, y);
    tft.print(vacSelected ? ">" : " ");
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(labelX, y);
    tft.print("Vacation:");
    tft.setTextColor(vacSelected ? Colors::ACCENT : (vacMode ? Colors::SUCCESS : Colors::TEXT), Colors::BACKGROUND);
    tft.setCursor(valueX, y);
    tft.print(vacMode ? "ON" : "OFF");
    y += lineHeight;

    // Time (inline edit)
    // When in selection mode, always show edit values (snapshot) for consistency
    bool timeSelected = selectionMode && menuIndex == 2;
    bool timeEditing = timeSelected && editingField;
    bool editingHour = timeEditing && editSubField == 0;
    bool editingMin = timeEditing && editSubField == 1;
    tft.setTextColor(timeSelected ? Colors::ACCENT : Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(5, y);
    tft.print(timeSelected ? ">" : " ");
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(labelX, y);
    tft.print("Time:");
    if (selectionMode) {
        // Show edit values - these are what will be saved
        tft.setTextColor(editingHour ? Colors::ACCENT : Colors::TEXT, Colors::BACKGROUND);
        tft.setCursor(valueX, y);
        tft.printf("%02d", editHour);
        tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
        tft.print(":");
        tft.setTextColor(editingMin ? Colors::ACCENT : Colors::TEXT, Colors::BACKGROUND);
        tft.printf("%02d", editMinute);
    } else {
        // Not in selection mode - show fresh RTC values
        tft.setTextColor(Colors::TEXT, Colors::BACKGROUND);
        tft.setCursor(valueX, y);
        tft.print(displayTime);
    }
    y += lineHeight;

    // Date (inline edit)
    // When in selection mode, always show edit values (snapshot) for consistency
    bool dateSelected = selectionMode && menuIndex == 3;
    bool dateEditing = dateSelected && editingField;
    bool editingMonth = dateEditing && editSubField == 0;
    bool editingDay = dateEditing && editSubField == 1;
    bool editingYear = dateEditing && editSubField == 2;
    tft.setTextColor(dateSelected ? Colors::ACCENT : Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(5, y);
    tft.print(dateSelected ? ">" : " ");
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(labelX, y);
    tft.print("Date:");
    if (selectionMode) {
        // Show edit values - these are what will be saved
        tft.setTextColor(editingMonth ? Colors::ACCENT : Colors::TEXT, Colors::BACKGROUND);
        tft.setCursor(valueX, y);
        tft.print(monthNames[editMonth]);
        tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
        tft.print(" ");
        tft.setTextColor(editingDay ? Colors::ACCENT : Colors::TEXT, Colors::BACKGROUND);
        tft.printf("%02d", editDay);
        tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
        tft.print(" ");
        tft.setTextColor(editingYear ? Colors::ACCENT : Colors::TEXT, Colors::BACKGROUND);
        tft.print(editYear);
    } else {
        // Not in selection mode - show fresh RTC values
        tft.setTextColor(Colors::TEXT, Colors::BACKGROUND);
        tft.setCursor(valueX, y);
        tft.print(displayDate);
    }
    y += lineHeight;

    // Reset
    bool resetSelected = selectionMode && menuIndex == 4;
    tft.setTextColor(resetSelected ? Colors::DANGER : Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(5, y);
    tft.print(resetSelected ? ">" : " ");
    tft.setCursor(labelX, y);
    tft.print("Reset All");

    // Instructions
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(10, 150);
    if (!selectionMode) {
        tft.print("[Hold] Edit");
    } else if (editingField) {
        tft.print("[Press] Adjust  [Hold] Next");
    } else if (menuIndex == 4) {
        tft.print("[Hold] Reset   [Hold Top] Save");
    } else if (menuIndex == 1) {
        tft.print("[Hold] Toggle  [Hold Top] Save");
    } else {
        tft.print("[Hold] Edit    [Hold Top] Save");
    }

    tft.setTextFont(1);
    drawNavCarets();
}

void Display::drawThrowConfirmScreen() {
    tft.fillScreen(Colors::BACKGROUND);

    // Warning colors
    tft.fillRect(0, 0, SCREEN_WIDTH, 30, Colors::WARNING);
    tft.setTextColor(Colors::BACKGROUND, Colors::WARNING);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setCursor(100, 6);
    tft.print("Manual Throw");

    tft.setTextColor(Colors::TEXT, Colors::BACKGROUND);
    tft.setCursor(30, 50);
    tft.println("Activate motor?");

    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(30, 75);
    tft.printf("Duration: %d seconds", MOTOR_DEFAULT_DURATION_SEC);

    // Action buttons
    tft.setTextColor(Colors::DANGER, Colors::BACKGROUND);
    tft.setCursor(30, 130);
    tft.print("[Hold Bot] GO");

    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(200, 130);
    tft.print("Cancel");
    tft.setTextFont(1);
}

void Display::drawSettingsEditScreen() {
    tft.fillScreen(Colors::BACKGROUND);
    drawHeader("Motor Time");

    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(Colors::TEXT, Colors::BACKGROUND);
    tft.setCursor(20, 50);
    tft.print("Duration (seconds):");

    // Large value display
    tft.setTextFont(1);
    tft.setTextSize(4);
    tft.setTextColor(Colors::ACCENT, Colors::BACKGROUND);
    tft.setCursor(120, 75);
    tft.printf("%d", editValue);

    // Hints
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(20, 145);
    tft.print("[Hold] Save/Cancel");
    tft.setTextFont(1);

    // Up/down carets
    tft.setTextSize(2);
    tft.setCursor(300, 30);
    tft.print("-");
    tft.setCursor(300, 140);
    tft.print("+");
}

void Display::drawTimeEditScreen() {
    tft.fillScreen(Colors::BACKGROUND);
    drawHeader("Set Time");

    // Large time display
    tft.setTextFont(1);
    tft.setTextSize(4);

    char hourStr[3], minStr[3];
    snprintf(hourStr, sizeof(hourStr), "%02d", editHour);
    snprintf(minStr, sizeof(minStr), "%02d", editMinute);

    int baseX = 80;
    int baseY = 60;

    // Hour (highlighted if editing)
    tft.setTextColor(editField == 0 ? Colors::ACCENT : Colors::TEXT, Colors::BACKGROUND);
    tft.setCursor(baseX, baseY);
    tft.print(hourStr);

    // Colon
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(baseX + 50, baseY);
    tft.print(":");

    // Minute (highlighted if editing)
    tft.setTextColor(editField == 1 ? Colors::ACCENT : Colors::TEXT, Colors::BACKGROUND);
    tft.setCursor(baseX + 75, baseY);
    tft.print(minStr);

    // Field indicator arrows
    tft.setTextSize(2);
    int arrowX = editField == 0 ? baseX + 12 : baseX + 87;
    tft.setTextColor(Colors::ACCENT, Colors::BACKGROUND);
    tft.setCursor(arrowX, baseY - 25);
    tft.print("^");
    tft.setCursor(arrowX, baseY + 35);
    tft.print("v");

    // Instructions
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(20, 130);
    tft.print("[Hold] ");
    tft.print(editField == 0 ? "Next" : "Save");
    tft.setCursor(180, 130);
    tft.print("[Hold Top] Cancel");

    tft.setTextFont(1);
}

void Display::drawDateEditScreen() {
    tft.fillScreen(Colors::BACKGROUND);
    drawHeader("Set Date");

    // Month names
    static const char* months[] = {"", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    tft.setTextFont(1);
    tft.setTextSize(3);

    int baseY = 65;
    int monthX = 30;
    int dayX = 120;
    int yearX = 190;

    // Month
    tft.setTextColor(editField == 0 ? Colors::ACCENT : Colors::TEXT, Colors::BACKGROUND);
    tft.setCursor(monthX, baseY);
    tft.print(months[editMonth]);

    // Day
    tft.setTextColor(editField == 1 ? Colors::ACCENT : Colors::TEXT, Colors::BACKGROUND);
    tft.setCursor(dayX, baseY);
    tft.printf("%02d", editDay);

    // Year
    tft.setTextColor(editField == 2 ? Colors::ACCENT : Colors::TEXT, Colors::BACKGROUND);
    tft.setCursor(yearX, baseY);
    tft.print(editYear);

    // Field indicator arrows
    tft.setTextSize(2);
    int arrowX;
    if (editField == 0) arrowX = monthX + 20;
    else if (editField == 1) arrowX = dayX + 10;
    else arrowX = yearX + 25;

    tft.setTextColor(Colors::ACCENT, Colors::BACKGROUND);
    tft.setCursor(arrowX, baseY - 22);
    tft.print("^");
    tft.setCursor(arrowX, baseY + 28);
    tft.print("v");

    // Instructions
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(20, 130);
    tft.print("[Hold] ");
    tft.print(editField < 2 ? "Next" : "Save");
    tft.setCursor(180, 130);
    tft.print("[Hold Top] Cancel");

    tft.setTextFont(1);
}

void Display::drawConnectivityScreen() {
    tft.fillScreen(Colors::BACKGROUND);
    drawHeader("Connectivity");

    tft.setTextFont(2);
    tft.setTextSize(1);

    bool wifiOn = wifiManager.isRunning();
    int y = 32;

    // WiFi status (actual state from wifiManager)
    tft.setTextColor(Colors::TEXT, Colors::BACKGROUND);
    tft.setCursor(20, y);
    tft.print("WiFi: ");
    tft.setTextColor(wifiOn ? Colors::SUCCESS : Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.print(wifiOn ? "On" : "Off");
    y += 22;

    // SSID
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(20, y);
    tft.print("SSID:");
    y += 16;
    tft.setTextColor(Colors::ACCENT, Colors::BACKGROUND);
    tft.setCursor(20, y);
    tft.print(pairingSSID[0] ? pairingSSID : "FeedMe-XXXX");
    y += 22;

    // Password
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(20, y);
    tft.print("Pass:");
    y += 16;
    tft.setTextColor(Colors::SUCCESS, Colors::BACKGROUND);
    tft.setCursor(20, y);
    tft.print(pairingPassword[0] ? pairingPassword : "--------");
    y += 22;

    // IP Address (only when WiFi is on)
    if (wifiOn) {
        tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
        tft.setCursor(20, y);
        tft.print("IP: ");
        tft.setTextColor(Colors::ACCENT, Colors::BACKGROUND);
        tft.print(wifiManager.getIP().toString());
    }

    // Draw QR code only when WiFi is on
    if (wifiOn) {
        const char* ssid = pairingSSID[0] ? pairingSSID : "FeedMe-XXXX";
        const char* pass = pairingPassword[0] ? pairingPassword : "--------";

        // Build WiFi QR code string: WIFI:T:WPA;S:<ssid>;P:<password>;;
        char qrData[96];
        snprintf(qrData, sizeof(qrData), "WIFI:T:WPA;S:%s;P:%s;;", ssid, pass);

        // Generate QR code (version 3 = 29x29 modules)
        QRCode qrcode;
        uint8_t qrcodeData[qrcode_getBufferSize(3)];
        qrcode_initText(&qrcode, qrcodeData, 3, ECC_LOW, qrData);

        // Draw QR code - position on right side of screen (left of nav carets)
        const int qrPixelSize = 4;  // Each QR module = 4x4 pixels
        const int qrSize = qrcode.size * qrPixelSize;
        const int qrX = SCREEN_WIDTH - qrSize - 35;  // Right side, leaving room for carets
        const int qrY = 30;  // Below header

        // White background for QR code
        tft.fillRect(qrX - 4, qrY - 4, qrSize + 8, qrSize + 8, TFT_WHITE);

        // Draw QR modules
        for (int qy = 0; qy < qrcode.size; qy++) {
            for (int qx = 0; qx < qrcode.size; qx++) {
                if (qrcode_getModule(&qrcode, qx, qy)) {
                    tft.fillRect(qrX + qx * qrPixelSize, qrY + qy * qrPixelSize,
                                qrPixelSize, qrPixelSize, TFT_BLACK);
                }
            }
        }
    }

    // Instructions
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(10, 150);
    tft.print(wifiOn ? "[Hold] Disable" : "[Hold] Enable");

    tft.setTextFont(1);
    drawNavCarets();
}

void Display::drawWifiSchedulesScreen() {
    tft.fillScreen(Colors::BACKGROUND);
    drawHeader("WiFi Schedules");

    tft.setTextFont(2);
    tft.setTextSize(1);

    int y = 35;
    int wifiScheduleCount = storage.getWifiScheduleCount();

    // Explanation text
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(20, y);
    tft.print("WiFi auto-enables during:");
    y += 20;

    // Show WiFi schedules
    for (int i = 0; i < wifiScheduleCount && i < 3; i++) {
        WifiSchedule* s = storage.getWifiSchedule(i);
        if (s) {
            char timeBuf[24];
            snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d - %02d:%02d",
                     s->startHour, s->startMinute, s->endHour, s->endMinute);

            tft.setTextColor(selectionMode && menuIndex == i ? Colors::ACCENT : Colors::TEXT_DIM, Colors::BACKGROUND);
            tft.setCursor(5, y);
            tft.print(selectionMode && menuIndex == i ? ">" : " ");

            tft.setTextColor(s->enabled ? Colors::TEXT : Colors::TEXT_DIM, Colors::BACKGROUND);
            tft.setCursor(20, y);
            tft.print(timeBuf);

            if (!s->enabled) {
                tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
                tft.print(" (off)");
            }
            y += 22;
        }
    }

    // Add New option
    if (wifiScheduleCount < 8) {
        drawMenuItem(y, "+ Add New", "", selectionMode && menuIndex == wifiScheduleCount);
    }

    // Instructions
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(10, 150);
    if (!selectionMode) {
        tft.print("[Hold] Edit");
    } else {
        tft.print("[Hold] Select  [Hold Top] Back");
    }

    tft.setTextFont(1);
    drawNavCarets();
}

void Display::drawWifiScheduleEditScreen() {
    tft.fillScreen(Colors::BACKGROUND);
    drawHeader("Edit WiFi Sched");

    tft.setTextFont(2);
    tft.setTextSize(1);

    int y = 32;
    const int lineHeight = 22;
    const int labelX = 20;
    const int valueX = 100;

    // Start Time
    bool startSelected = (editField == 0);
    bool startEditing = startSelected && editingField;
    bool editingStartHour = startEditing && editSubField == 0;
    bool editingStartMin = startEditing && editSubField == 1;
    tft.setTextColor(startSelected ? Colors::ACCENT : Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(5, y);
    tft.print(startSelected ? ">" : " ");
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(labelX, y);
    tft.print("Start:");
    tft.setTextColor(editingStartHour ? Colors::ACCENT : Colors::TEXT, Colors::BACKGROUND);
    tft.setCursor(valueX, y);
    tft.printf("%02d", editStartHour);
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.print(":");
    tft.setTextColor(editingStartMin ? Colors::ACCENT : Colors::TEXT, Colors::BACKGROUND);
    tft.printf("%02d", editStartMinute);
    y += lineHeight;

    // End Time
    bool endSelected = (editField == 1);
    bool endEditing = endSelected && editingField;
    bool editingEndHour = endEditing && editSubField == 0;
    bool editingEndMin = endEditing && editSubField == 1;
    tft.setTextColor(endSelected ? Colors::ACCENT : Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(5, y);
    tft.print(endSelected ? ">" : " ");
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(labelX, y);
    tft.print("End:");
    tft.setTextColor(editingEndHour ? Colors::ACCENT : Colors::TEXT, Colors::BACKGROUND);
    tft.setCursor(valueX, y);
    tft.printf("%02d", editEndHour);
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.print(":");
    tft.setTextColor(editingEndMin ? Colors::ACCENT : Colors::TEXT, Colors::BACKGROUND);
    tft.printf("%02d", editEndMinute);
    y += lineHeight;

    // Days
    bool daysSelected = (editField == 2);
    tft.setTextColor(daysSelected ? Colors::ACCENT : Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(5, y);
    tft.print(daysSelected ? ">" : " ");
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(labelX, y);
    tft.print("Days:");
    tft.setTextColor(Colors::TEXT, Colors::BACKGROUND);
    tft.setCursor(valueX, y);
    if (editDays == 0x7F) tft.print("Daily");
    else if (editDays == 0x3E) tft.print("Weekdays");
    else if (editDays == 0x41) tft.print("Weekends");
    else tft.print("Custom");
    y += lineHeight;

    // Enabled
    bool enabledSelected = (editField == 3);
    tft.setTextColor(enabledSelected ? Colors::ACCENT : Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(5, y);
    tft.print(enabledSelected ? ">" : " ");
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(labelX, y);
    tft.print("Enabled:");
    tft.setTextColor(editEnabled ? Colors::SUCCESS : Colors::DANGER, Colors::BACKGROUND);
    tft.setCursor(valueX, y);
    tft.print(editEnabled ? "ON" : "OFF");
    y += lineHeight;

    // Delete
    bool deleteSelected = (editField == 4);
    tft.setTextColor(deleteSelected ? Colors::DANGER : Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(5, y);
    tft.print(deleteSelected ? ">" : " ");
    tft.setCursor(labelX, y);
    tft.print("Delete");

    // Instructions
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(10, 150);
    if (editingField) {
        tft.print("[Press] Adjust  [Hold] Next");
    } else if (editField == 4) {
        tft.print("[Hold] Delete  [Hold Top] Save");
    } else if (editField == 2 || editField == 3) {
        tft.print("[Hold] Toggle  [Hold Top] Save");
    } else {
        tft.print("[Hold] Edit    [Hold Top] Save");
    }

    tft.setTextFont(1);
}

void Display::drawAboutScreen() {
    tft.fillScreen(Colors::BACKGROUND);
    drawHeader("About");

    tft.setTextFont(2);
    tft.setTextSize(1);

    int y = 40;

    tft.setTextColor(Colors::ACCENT, Colors::BACKGROUND);
    tft.setCursor(20, y);
    tft.print("FeedMe");
    y += 25;

    tft.setTextColor(Colors::TEXT, Colors::BACKGROUND);
    tft.setCursor(20, y);
    tft.print("Version: ");
    tft.print(FEEDME_VERSION);
    y += 20;

    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(20, y);
    tft.print("Device ID: ");
    tft.setTextColor(Colors::ACCENT, Colors::BACKGROUND);
    tft.print(storage.getDeviceId());

    tft.setTextFont(1);
    drawNavCarets();
}

void Display::drawConfirmResetScreen() {
    tft.fillScreen(Colors::BACKGROUND);

    // Warning header
    tft.fillRect(0, 0, SCREEN_WIDTH, 30, Colors::DANGER);
    tft.setTextColor(Colors::TEXT, Colors::DANGER);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setCursor(80, 6);
    tft.print("Reset Settings?");

    tft.setTextColor(Colors::TEXT, Colors::BACKGROUND);
    tft.setCursor(20, 50);
    tft.print("This will erase all");
    tft.setCursor(20, 70);
    tft.print("schedules and settings.");

    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(20, 100);
    tft.print("Time will be preserved.");

    // Action hints
    tft.setTextColor(Colors::DANGER, Colors::BACKGROUND);
    tft.setCursor(20, 140);
    tft.print("[Hold Bot] RESET");

    tft.setTextColor(Colors::TEXT_DIM, Colors::BACKGROUND);
    tft.setCursor(200, 140);
    tft.print("Cancel");

    tft.setTextFont(1);
}
