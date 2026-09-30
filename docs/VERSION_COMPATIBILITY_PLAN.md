# FeedMe Version Compatibility Plan

> **Status**: Planning (not yet implemented)
> **Created**: 2025-01-24
> **Purpose**: Handle version mismatches between iOS app and ESP32 firmware gracefully

## Problem Statement

When the iOS app and ESP32 firmware can be updated independently, version mismatches can occur:
- iOS app may have features the firmware doesn't support
- Firmware may send data the app doesn't understand
- Without proper handling, this causes crashes or broken UI

## Proposed Solution: Protocol Version + Versioned Capabilities

### DeviceInfo Response Format

```json
{
    "version": "1.2.0",           // Firmware version (display only)
    "deviceId": "XXXX",
    "locked": false,
    "pinSet": true,
    "protocolVersion": 2,          // Breaking change indicator
    "capabilities": {
        "bleSchedules": 1,         // Version 1: basic start/end times
        "feedSchedules": 2,        // Version 2: added seasonal date ranges
        "lightSleep": 1,           // Version 1: basic timeout
        "sunCalc": 1,              // Version 1: sunrise/sunset
        "ota": 1                   // Version 1: basic OTA
    }
}
```

### Version Layers

| Layer | Purpose | When to Increment |
|-------|---------|-------------------|
| `protocolVersion` | Breaking changes | New required fields, removed characteristics, changed data types |
| `capabilities[feature]` | Feature evolution | New optional fields within a feature, enhanced behavior |
| Additive fields | Minor additions | Just add field, no version change needed |

## Implementation Rules

### ESP32 (Server)
1. Always send all fields (even if client is old)
2. Ignore unknown fields in requests
3. Use safe defaults for missing optional fields
4. Increment capability version when adding fields that change behavior

### iOS (Client)
1. Check `protocolVersion` on connect - warn if too old/new
2. Check capability versions before showing features
3. Use `decodeIfPresent` with defaults for all optional fields
4. Hide UI for unsupported capabilities

## Capability Version History

### bleSchedules
- **v1**: Basic fields (startHour, startMinute, endHour, endMinute, days, enabled)

### feedSchedules
- **v1**: Basic fields (hour, minute, days, enabled, name)
- **v2**: Added seasonal date ranges (startMonth, startDay, endMonth, endDay)

### lightSleep
- **v1**: Basic inactivity timeout (inactivityTimeoutMin)

### sunCalc
- **v1**: Sunrise/sunset with offset (scheduleType, sunOffset)

### ota
- **v1**: Basic WiFi OTA update support

## iOS Implementation Example

```swift
// In DeviceInfo model
struct Capabilities: Codable {
    var bleSchedules: Int?
    var feedSchedules: Int?
    var lightSleep: Int?
    var sunCalc: Int?
    var ota: Int?
}

// In DeviceViewModel
var supportsLightSleep: Bool {
    (deviceInfo?.capabilities?.lightSleep ?? 0) >= 1
}

var supportsBleScheduleNames: Bool {
    (deviceInfo?.capabilities?.bleSchedules ?? 0) >= 2
}

var supportsOTA: Bool {
    (deviceInfo?.capabilities?.ota ?? 0) >= 1
}
```

```swift
// In SettingsView - conditional UI
if viewModel.supportsLightSleep {
    powerSection  // Show inactivity timeout slider
}

if viewModel.supportsOTA {
    otaSection  // Show firmware update option
}
```

## Migration Path

1. **Phase 1**: Add `protocolVersion` and `capabilities` to DeviceInfo (ESP32)
2. **Phase 2**: Update iOS to parse capabilities, add feature checks
3. **Phase 3**: Implement OTA with `ota` capability check
4. **Future**: Increment capability versions as features evolve

## Graceful Degradation Scenarios

| Scenario | Behavior |
|----------|----------|
| iOS newer, firmware lacks capability | Hide feature in UI, show "Update firmware" hint |
| iOS older, firmware has new capability | iOS ignores unknown capability (no crash) |
| Protocol version mismatch (major) | Show warning dialog, suggest update |
| Missing optional JSON field | Use default value, continue normally |

## References

- [API Versioning Best Practices](https://www.averagedevs.com/blog/api-versioning-backward-compatibility)
- [Schema Evolution Strategies](https://app.studyraid.com/en/read/12384/399934/schema-versioning-strategies)
- [Protobuf Editions (per-feature lifecycle)](https://protobuf.dev/editions/overview/)
- [gRPC Versioning Guidance](https://learn.microsoft.com/en-us/aspnet/core/grpc/versioning)
