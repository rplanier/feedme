#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// Template-based schedule manager to eliminate duplication between
// feed schedules and BLE schedules
template<typename T, int MAX_COUNT>
class ScheduleManager {
public:
    int getCount() const { return count; }

    T* get(int index) {
        if (index < 0 || index >= count) {
            return nullptr;
        }
        return &items[index];
    }

    T* getById(uint16_t id) {
        for (int i = 0; i < count; i++) {
            if (items[i].id == id) {
                return &items[i];
            }
        }
        return nullptr;
    }

    bool add(const T& item) {
        if (count >= MAX_COUNT) {
            DEBUG_PRINTLN("ScheduleManager: Max items reached");
            return false;
        }

        items[count] = item;
        items[count].id = nextId++;
        count++;
        return true;
    }

    bool update(uint16_t id, const T& item) {
        T* existing = getById(id);
        if (!existing) {
            return false;
        }

        *existing = item;
        existing->id = id;  // Preserve original ID
        return true;
    }

    bool remove(uint16_t id) {
        int index = -1;
        for (int i = 0; i < count; i++) {
            if (items[i].id == id) {
                index = i;
                break;
            }
        }

        if (index < 0) {
            return false;
        }

        // Shift remaining items
        for (int i = index; i < count - 1; i++) {
            items[i] = items[i + 1];
        }
        count--;
        return true;
    }

    // Load from JSON file
    // jsonKey is the root key in the JSON (e.g., "schedules" or "bleSchedules")
    // deserializeItem is a function that populates an item from a JsonObject
    template<typename DeserializeFunc>
    void load(const char* filename, const char* jsonKey, DeserializeFunc deserializeItem) {
        File file = LittleFS.open(filename, "r");
        if (!file) {
            DEBUG_PRINTF("ScheduleManager: No file found: %s\n", filename);
            count = 0;
            return;
        }

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, file);
        file.close();

        if (error) {
            DEBUG_PRINTF("ScheduleManager: Failed to parse %s: %s\n", filename, error.c_str());
            count = 0;
            return;
        }

        JsonArray arr = doc[jsonKey].as<JsonArray>();
        count = 0;
        nextId = 1;

        for (JsonObject obj : arr) {
            if (count >= MAX_COUNT) break;

            T& item = items[count];
            deserializeItem(item, obj);

            // Update nextId to be higher than any loaded ID
            if (item.id >= nextId) {
                nextId = item.id + 1;
            }

            count++;
        }

        DEBUG_PRINTF("ScheduleManager: Loaded %d items from %s\n", count, filename);
    }

    // Save to JSON file
    // jsonKey is the root key in the JSON (e.g., "schedules" or "bleSchedules")
    // serializeItem is a function that writes an item to a JsonObject
    template<typename SerializeFunc>
    void save(const char* filename, const char* jsonKey, SerializeFunc serializeItem) {
        JsonDocument doc;
        JsonArray arr = doc[jsonKey].to<JsonArray>();

        for (int i = 0; i < count; i++) {
            JsonObject obj = arr.add<JsonObject>();
            serializeItem(items[i], obj);
        }

        File file = LittleFS.open(filename, "w");
        if (!file) {
            DEBUG_PRINTF("ScheduleManager: Failed to open %s for writing\n", filename);
            return;
        }

        serializeJson(doc, file);
        file.close();
        DEBUG_PRINTF("ScheduleManager: Saved %d items to %s\n", count, filename);
    }

    // Convert to JSON array string (for API responses)
    template<typename SerializeFunc>
    String toJson(SerializeFunc serializeItem) const {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();

        for (int i = 0; i < count; i++) {
            JsonObject obj = arr.add<JsonObject>();
            serializeItem(items[i], obj);
        }

        String result;
        serializeJson(doc, result);
        return result;
    }

    // Reset to empty
    void clear() {
        count = 0;
        nextId = 1;
    }

    // Get next ID (for external use if needed)
    uint16_t getNextId() const { return nextId; }

private:
    T items[MAX_COUNT];
    int count = 0;
    uint16_t nextId = 1;
};
