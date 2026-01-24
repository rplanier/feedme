#pragma once

#include <Arduino.h>
#include <cmath>

// Sunrise/Sunset calculation based on NOAA algorithm
// Returns time as minutes since midnight (0-1439)

class SunCalc {
public:
    // Calculate sunrise time for a given date and location
    // Returns minutes since midnight in LOCAL time (after applying tzOffset)
    // Returns -1 if sun doesn't rise (polar night)
    static int getSunrise(int year, int month, int day, float latitude, float longitude, int16_t tzOffsetMinutes) {
        return calculateSunTime(year, month, day, latitude, longitude, tzOffsetMinutes, true);
    }

    // Calculate sunset time for a given date and location
    // Returns minutes since midnight in LOCAL time (after applying tzOffset)
    // Returns -1 if sun doesn't set (midnight sun)
    static int getSunset(int year, int month, int day, float latitude, float longitude, int16_t tzOffsetMinutes) {
        return calculateSunTime(year, month, day, latitude, longitude, tzOffsetMinutes, false);
    }

    // Get hour and minute from minutes since midnight
    static void minutesToHourMin(int minutes, int& hour, int& minute) {
        if (minutes < 0) {
            hour = 0;
            minute = 0;
            return;
        }
        hour = (minutes / 60) % 24;
        minute = minutes % 60;
    }

private:
    static constexpr float DEG2RAD = M_PI / 180.0f;
    static constexpr float RAD2DEG = 180.0f / M_PI;

    // Calculate Julian day from calendar date
    static float julianDay(int year, int month, int day) {
        if (month <= 2) {
            year -= 1;
            month += 12;
        }
        int A = year / 100;
        int B = 2 - A + A / 4;
        return floor(365.25f * (year + 4716)) + floor(30.6001f * (month + 1)) + day + B - 1524.5f;
    }

    // Calculate sun time (sunrise or sunset)
    // tzOffsetMinutes: negative = west of UTC (like Swift's secondsFromGMT), so local = UTC + offset
    static int calculateSunTime(int year, int month, int day, float latitude, float longitude, int16_t tzOffsetMinutes, bool sunrise) {
        // Julian day
        float jd = julianDay(year, month, day);

        // Julian century
        float t = (jd - 2451545.0f) / 36525.0f;

        // Geometric mean longitude of sun (degrees)
        float L0 = fmod(280.46646f + t * (36000.76983f + 0.0003032f * t), 360.0f);

        // Geometric mean anomaly of sun (degrees)
        float M = fmod(357.52911f + t * (35999.05029f - 0.0001537f * t), 360.0f);

        // Eccentricity of earth's orbit
        float e = 0.016708634f - t * (0.000042037f + 0.0000001267f * t);

        // Sun's equation of center
        float C = sin(M * DEG2RAD) * (1.914602f - t * (0.004817f + 0.000014f * t))
                + sin(2.0f * M * DEG2RAD) * (0.019993f - 0.000101f * t)
                + sin(3.0f * M * DEG2RAD) * 0.000289f;

        // Sun's true longitude
        float sunLon = L0 + C;

        // Sun's apparent longitude
        float omega = 125.04f - 1934.136f * t;
        float lambda = sunLon - 0.00569f - 0.00478f * sin(omega * DEG2RAD);

        // Mean obliquity of ecliptic
        float obliq = 23.0f + (26.0f + (21.448f - t * (46.8150f + t * (0.00059f - t * 0.001813f))) / 60.0f) / 60.0f;

        // Corrected obliquity
        float obliqCorr = obliq + 0.00256f * cos(omega * DEG2RAD);

        // Sun's declination
        float sinDec = sin(obliqCorr * DEG2RAD) * sin(lambda * DEG2RAD);
        float cosDec = cos(asin(sinDec));

        // Equation of time (minutes)
        float y = tan(obliqCorr * DEG2RAD / 2.0f);
        y = y * y;
        float eqTime = 4.0f * RAD2DEG * (y * sin(2.0f * L0 * DEG2RAD)
                     - 2.0f * e * sin(M * DEG2RAD)
                     + 4.0f * e * y * sin(M * DEG2RAD) * cos(2.0f * L0 * DEG2RAD)
                     - 0.5f * y * y * sin(4.0f * L0 * DEG2RAD)
                     - 1.25f * e * e * sin(2.0f * M * DEG2RAD));

        // Hour angle for sunrise/sunset
        float latRad = latitude * DEG2RAD;
        float zenith = 90.833f;  // Official zenith for sunrise/sunset (accounts for refraction)

        float cosHA = (cos(zenith * DEG2RAD) / (cos(latRad) * cosDec)) - tan(latRad) * sinDec / cosDec;

        // Check if sun rises/sets at this location on this date
        if (cosHA > 1.0f) {
            return -1;  // Sun doesn't rise (polar night)
        }
        if (cosHA < -1.0f) {
            return -1;  // Sun doesn't set (midnight sun)
        }

        float HA = acos(cosHA) * RAD2DEG;

        // Solar noon (minutes from midnight UTC)
        float solarNoon = (720.0f - 4.0f * longitude - eqTime);

        // Sunrise or sunset time in minutes from midnight UTC
        float sunTime;
        if (sunrise) {
            sunTime = solarNoon - HA * 4.0f;
        } else {
            sunTime = solarNoon + HA * 4.0f;
        }

        // Convert from UTC to local time
        // tzOffsetMinutes is negative for locations west of UTC (e.g., US timezones)
        // Local time = UTC time + offset
        float localTime = sunTime + tzOffsetMinutes;

        // Normalize to 0-1440 range
        while (localTime < 0) localTime += 1440.0f;
        while (localTime >= 1440) localTime -= 1440.0f;

        // Use floor() for more conservative timing (sunrise slightly later, sunset slightly earlier)
        // This ensures feeds happen before sunset rather than after
        return (int)floor(localTime);
    }
};
