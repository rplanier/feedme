#pragma once

#include <Arduino.h>

// =============================================================================
// Time Formatting Utilities
// Centralized 12-hour time formatting to eliminate code duplication
// =============================================================================

struct FormattedTime {
    int displayHour;
    const char* ampm;

    FormattedTime(int hour24) {
        if (hour24 == 0) {
            displayHour = 12;
            ampm = "AM";
        } else if (hour24 == 12) {
            displayHour = 12;
            ampm = "PM";
        } else if (hour24 > 12) {
            displayHour = hour24 - 12;
            ampm = "PM";
        } else {
            displayHour = hour24;
            ampm = "AM";
        }
    }
};

// Format time as "H:MM AM" or "H:MM PM"
inline void formatTime12Hour(char* buffer, size_t len, int hour24, int minute) {
    FormattedTime ft(hour24);
    snprintf(buffer, len, "%d:%02d %s", ft.displayHour, minute, ft.ampm);
}

// Format next feed display string like "Today 7:00 AM" or "Tomorrow 6:30 PM"
inline void formatNextFeedDisplay(char* buffer, size_t len, int hour24, int minute, int daysAway, int dayOfWeek) {
    FormattedTime ft(hour24);

    if (daysAway == 0) {
        snprintf(buffer, len, "Today %d:%02d %s", ft.displayHour, minute, ft.ampm);
    } else if (daysAway == 1) {
        snprintf(buffer, len, "Tomorrow %d:%02d %s", ft.displayHour, minute, ft.ampm);
    } else {
        snprintf(buffer, len, "In %d days %d:%02d %s", daysAway, ft.displayHour, minute, ft.ampm);
    }
}

// Format time as "HH:MM" (24-hour with leading zeros)
inline void formatTime24Hour(char* buffer, size_t len, int hour, int minute) {
    snprintf(buffer, len, "%02d:%02d", hour, minute);
}
