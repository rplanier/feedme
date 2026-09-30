# FeedMe Native iOS App Architecture Plan

## Overview

Transition from WiFi-based web interface to BLE-only native iOS app for all user interactions. WiFi retained only for OTA firmware updates.

---

## Current State

**ESP32 Firmware:**
- BLE GATT service with single "Wake" characteristic
- WiFi AP with full web server and REST API
- Web UI (HTML/CSS/JS) served from LittleFS

**User Flow (Current):**
1. User opens BLE app (LightBlue)
2. Writes to Wake characteristic
3. ESP32 starts WiFi AP
4. User connects phone to WiFi
5. User opens browser to 192.168.4.1
6. Configures feeder via web UI

---

## Target State

**ESP32 Firmware:**
- Expanded BLE GATT service for all operations
- Minimal WiFi (OTA endpoint only, no web UI)
- No web files in LittleFS (or minimal status page)

**User Flow (Target):**
1. User opens FeedMe iOS app
2. App connects via BLE
3. All configuration done in native app
4. For OTA: App triggers WiFi → uploads firmware → WiFi disables

---

## Architecture Components

### 1. ESP32 BLE GATT Service Design

**Service UUID:** `f33d0001-1234-5678-9abc-def012345678`

| Characteristic | UUID | Properties | Description |
|----------------|------|------------|-------------|
| **Device Info** | `f33d0010-1234-5678-9abc-def012345678` | Read | JSON: version, deviceId, locked status (always readable) |
| **Auth** | `f33d0011-1234-5678-9abc-def012345678` | Write, Notify | Write PIN to unlock; notifies with success/failure |
| **Status** | `f33d0012-1234-5678-9abc-def012345678` | Read, Notify | JSON: battery%, voltage, charging, time, nextFeed |
| **Settings** | `f33d0013-1234-5678-9abc-def012345678` | Read, Write | JSON: motorDuration, vacationMode, batteryType, location |
| **Feed Schedules** | `f33d0014-1234-5678-9abc-def012345678` | Read, Write | JSON array of feed schedules |
| **Feed Command** | `f33d0015-1234-5678-9abc-def012345678` | Write | Write duration (uint8) to trigger feed |
| **Feed History** | `f33d0016-1234-5678-9abc-def012345678` | Read | JSON array of recent feed events |
| **Time Sync** | `f33d0017-1234-5678-9abc-def012345678` | Write | JSON: epoch, tzOffset |
| **WiFi OTA** | `f33d0018-1234-5678-9abc-def012345678` | Write | Write 1 to enable WiFi for OTA |
| **Set PIN** | `f33d0019-1234-5678-9abc-def012345678` | Write | Change PIN (requires current auth) |
| **BLE Schedules** | `f33d001a-1234-5678-9abc-def012345678` | Read, Write | JSON array of BLE advertising schedules |

**Data Format Considerations:**
- BLE MTU is typically 20-512 bytes
- For large data (schedules, history): use chunked transfer or negotiate larger MTU
- JSON keeps it simple and debuggable
- Alternative: binary protocol for efficiency (more complex)

### 2. iOS App Architecture

**Framework:** Swift + SwiftUI + CoreBluetooth

**Project Structure:**
```
FeedMe/
├── App/
│   └── FeedMeApp.swift          # App entry point
├── Models/
│   ├── Device.swift             # Connected device model
│   ├── FeedSchedule.swift       # Feed schedule model
│   ├── BleSchedule.swift        # BLE advertising schedule model
│   ├── FeedEvent.swift          # History event model
│   └── Settings.swift           # Device settings model
├── Services/
│   ├── BluetoothManager.swift   # CoreBluetooth wrapper
│   ├── DeviceService.swift      # High-level device operations
│   └── OTAService.swift         # Firmware update handling
├── Views/
│   ├── DeviceListView.swift     # Scan and connect
│   ├── DashboardView.swift      # Status overview (like Home)
│   ├── FeedSchedulesView.swift  # Manage feed schedules
│   ├── BleSchedulesView.swift   # Manage BLE advertising schedules
│   ├── SettingsView.swift       # Device settings + location
│   ├── HistoryView.swift        # Feed history
│   └── OTAView.swift            # Firmware update UI
├── ViewModels/
│   ├── DeviceListViewModel.swift
│   ├── DashboardViewModel.swift
│   └── ...
└── Utilities/
    ├── Constants.swift          # UUIDs, etc.
    └── Extensions.swift
```

**Key iOS Classes:**

```swift
// BluetoothManager.swift
class BluetoothManager: NSObject, ObservableObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    @Published var discoveredDevices: [CBPeripheral] = []
    @Published var connectedDevice: CBPeripheral?
    @Published var connectionState: ConnectionState = .disconnected

    func startScanning()
    func connect(to peripheral: CBPeripheral)
    func disconnect()

    // Characteristic operations
    func readStatus() async throws -> DeviceStatus
    func readFeedSchedules() async throws -> [FeedSchedule]
    func writeFeedSchedules(_ schedules: [FeedSchedule]) async throws
    func readBleSchedules() async throws -> [BleSchedule]
    func writeBleSchedules(_ schedules: [BleSchedule]) async throws
    func writeSettings(_ settings: Settings) async throws
    func triggerFeed(duration: Int) async throws
    func syncTime() async throws
    func enableWiFiForOTA() async throws
}
```

### 3. OTA Update Flow

1. User taps "Update Firmware" in app
2. App sends write to WiFi OTA characteristic (value: 1)
3. ESP32 enables WiFi AP, responds with SSID/password via BLE
4. App prompts user to connect to FeedMe WiFi
5. App uploads .bin to `http://192.168.4.1/api/ota`
6. ESP32 installs update, reboots
7. ESP32 comes back in BLE mode
8. App reconnects via BLE

**Alternative:** iOS can programmatically join WiFi networks (NEHotspotConfiguration), avoiding manual WiFi switch.

---

## GATT Characteristic Details

**IMPORTANT:** All JSON examples below show the EXACT format used by the ESP32. The iOS app must parse these formats exactly.

---

### Device Info (UUID: `f33d0010-...`) - Read Only, No Auth Required

**Read Response:**
```json
{
  "version": "0.1b",
  "deviceId": "Y5N6",
  "locked": true,
  "pinSet": true
}
```

| Field | Type | Description |
|-------|------|-------------|
| `version` | string | Firmware version |
| `deviceId` | string | 4-character unique device ID |
| `locked` | boolean | `true` if session is locked (need to authenticate) |
| `pinSet` | boolean | `true` if a PIN has been configured |

**Notes:**
- Always readable, even before authentication
- After successful auth, `locked` becomes `false`
- If `pinSet` is `false`, device is unlocked by default (first-time setup)

---

### Auth (UUID: `f33d0011-...`) - Write + Notify

**Write (iOS → ESP32):**
```json
{"pin":"1234"}
```

**Notify Response (ESP32 → iOS) - Success:**
```json
{"success":true}
```

**Notify Response (ESP32 → iOS) - Failure:**
```json
{"success":false,"attemptsRemaining":4,"lockoutSeconds":0}
```

**Notify Response (ESP32 → iOS) - Locked Out:**
```json
{"success":false,"attemptsRemaining":0,"lockoutSeconds":45}
```

| Field | Type | Description |
|-------|------|-------------|
| `pin` | string | 4-6 digit PIN (digits only) |
| `success` | boolean | `true` if PIN was correct |
| `attemptsRemaining` | integer | Attempts left before lockout (0-5) |
| `lockoutSeconds` | integer | Seconds until lockout expires (0 if not locked) |

**Notes:**
- Subscribe to notifications before writing
- 5 failed attempts triggers 60-second lockout
- Session unlocks on success, remains unlocked until BLE disconnect

---

### Status (UUID: `f33d0012-...`) - Read Only, Requires Auth

**Read Response:**
```json
{
  "voltage": 12.45,
  "charging": false,
  "battery": 85,
  "time": "2024-01-22T14:30:00Z",
  "timeValid": true,
  "nextFeed": "Today 7:00 AM",
  "vacationMode": false
}
```

**Read Response (when unauthorized):**
```json
{"error":"unauthorized"}
```

| Field | Type | Description |
|-------|------|-------------|
| `voltage` | float | Battery voltage (e.g., 12.45 for 12V, 6.2 for 6V) |
| `charging` | boolean | `true` if solar panel is charging |
| `battery` | integer | Battery percentage (0-100) |
| `time` | string | Current device time in ISO 8601 format (UTC) |
| `timeValid` | boolean | `true` if time has been synced |
| `nextFeed` | string or null | Human-readable next feed time, or `null` if none scheduled |
| `vacationMode` | boolean | `true` if vacation mode is enabled |

**`nextFeed` Format Examples:**
- `"Today 7:00 AM"` - Feed scheduled for today
- `"Tomorrow 6:30 PM"` - Feed scheduled for tomorrow
- `"Mon 7:00 AM"` - Feed scheduled for a specific day
- `"In 3 days 7:00 AM"` - Feed more than 1 day away
- `null` - No schedules or vacation mode enabled

---

### Settings (UUID: `f33d0013-...`) - Read/Write, Requires Auth

**Read Response:**
```json
{
  "motorDuration": 5,
  "vacationMode": false,
  "batteryType": 0,
  "latitude": 29.82154,
  "longitude": -95.43607,
  "locationSet": true
}
```

**Write (iOS → ESP32) - Full Update:**
```json
{
  "motorDuration": 5,
  "vacationMode": false,
  "batteryType": 0,
  "latitude": 29.82154,
  "longitude": -95.43607
}
```

**Write (iOS → ESP32) - Partial Update (only changed fields):**
```json
{"vacationMode": true}
```

```json
{"motorDuration": 10}
```

```json
{"latitude": 32.7767, "longitude": -96.7970}
```

| Field | Type | Description |
|-------|------|-------------|
| `motorDuration` | integer | Default feed duration in seconds (1-30) |
| `vacationMode` | boolean | `true` to disable all scheduled feeds |
| `batteryType` | integer | Battery chemistry: `0`=SLA, `1`=AGM, `2`=GEL |
| `latitude` | float | GPS latitude for sunrise/sunset calculations |
| `longitude` | float | GPS longitude for sunrise/sunset calculations |
| `locationSet` | boolean | (Read only) `true` if location has been set |

**Battery Type Values:**
| Value | Name | Description |
|-------|------|-------------|
| `0` | SLA | Sealed Lead Acid (default) |
| `1` | AGM | Absorbent Glass Mat |
| `2` | GEL | Gel Cell |

---

### Feed Schedules (UUID: `f33d0014-...`) - Read/Write, Requires Auth

**Read Response (with schedules):**
```json
[
  {
    "id": 1,
    "name": "Daily at 7:00am",
    "hour": 13,
    "minute": 0,
    "days": 127,
    "enabled": true,
    "scheduleType": 0,
    "sunOffset": 0,
    "duration": 0,
    "startMonth": -1,
    "startDay": -1,
    "endMonth": -1,
    "endDay": -1
  },
  {
    "id": 2,
    "name": "Weekdays at sunrise",
    "hour": 12,
    "minute": 0,
    "days": 62,
    "enabled": true,
    "scheduleType": 1,
    "sunOffset": 0,
    "duration": 5,
    "startMonth": -1,
    "startDay": -1,
    "endMonth": -1,
    "endDay": -1
  },
  {
    "id": 3,
    "name": "Weekdays at sunset",
    "hour": 12,
    "minute": 0,
    "days": 62,
    "enabled": true,
    "scheduleType": 2,
    "sunOffset": 0,
    "duration": 5,
    "startMonth": -1,
    "startDay": -1,
    "endMonth": -1,
    "endDay": -1
  }
]
```

**Read Response (empty):**
```json
[]
```

**Write (iOS → ESP32) - Replace ALL schedules:**
```json
[
  {
    "id": 1,
    "name": "Morning Feed",
    "hour": 13,
    "minute": 0,
    "days": 127,
    "enabled": true,
    "scheduleType": 0,
    "sunOffset": 0,
    "duration": 0,
    "startMonth": -1,
    "startDay": -1,
    "endMonth": -1,
    "endDay": -1
  }
]
```

**Write (iOS → ESP32) - Clear all schedules:**
```json
[]
```

| Field | Type | Description |
|-------|------|-------------|
| `id` | integer | Unique schedule ID (auto-assigned if 0) |
| `name` | string | Display name (max 31 chars, auto-generated if empty) |
| `hour` | integer | Hour in UTC (0-23) - used when `scheduleType` = 0 |
| `minute` | integer | Minute (0-59) |
| `days` | integer | Bitmask for active days (see below) |
| `enabled` | boolean | `true` if schedule is active |
| `scheduleType` | integer | `0`=SPECIFIC_TIME, `1`=SUNRISE, `2`=SUNSET |
| `sunOffset` | integer | Minutes offset from sunrise/sunset (-120 to +120) |
| `duration` | integer | Feed duration override (0 = use default from settings) |
| `startMonth` | integer | Seasonal start month (1-12), or `-1` for no restriction |
| `startDay` | integer | Seasonal start day (1-31), or `-1` for no restriction |
| `endMonth` | integer | Seasonal end month (1-12), or `-1` for no restriction |
| `endDay` | integer | Seasonal end day (1-31), or `-1` for no restriction |

**Schedule Types:**
| Value | Name | Description |
|-------|------|-------------|
| `0` | SPECIFIC_TIME | Uses `hour` and `minute` fields (in UTC) |
| `1` | SUNRISE | Feeds at sunrise + `sunOffset` minutes |
| `2` | SUNSET | Feeds at sunset + `sunOffset` minutes |

**Days Bitmask:**
| Bit | Day | Example Values |
|-----|-----|----------------|
| 0 | Sunday | `1` = Sunday only |
| 1 | Monday | `2` = Monday only |
| 2 | Tuesday | `4` = Tuesday only |
| 3 | Wednesday | `8` = Wednesday only |
| 4 | Thursday | `16` = Thursday only |
| 5 | Friday | `32` = Friday only |
| 6 | Saturday | `64` = Saturday only |

**Common Day Combinations:**
| Value | Days |
|-------|------|
| `127` | All days (Sun-Sat) |
| `62` | Weekdays (Mon-Fri) |
| `65` | Weekends (Sat-Sun) |
| `0` | No days (disabled) |

**Seasonal Date Examples:**
- No restriction: `startMonth: -1, startDay: -1, endMonth: -1, endDay: -1`
- Hunting season (Oct 1 - Jan 31): `startMonth: 10, startDay: 1, endMonth: 1, endDay: 31`
- Summer only (Jun 1 - Aug 31): `startMonth: 6, startDay: 1, endMonth: 8, endDay: 31`

---

### BLE Schedules (UUID: `f33d001a-...`) - Read/Write, Requires Auth

Controls when BLE advertising is active. **No schedules = BLE always on (default).**

**Read Response (with schedules):**
```json
[
  {
    "id": 1,
    "name": "Morning Window",
    "startHour": 5,
    "startMinute": 0,
    "endHour": 9,
    "endMinute": 0,
    "days": 127,
    "enabled": true
  },
  {
    "id": 2,
    "name": "Evening Window",
    "startHour": 16,
    "startMinute": 0,
    "endHour": 20,
    "endMinute": 0,
    "days": 65,
    "enabled": true
  }
]
```

**Read Response (empty - BLE always on):**
```json
[]
```

**Write (iOS → ESP32) - Replace ALL BLE schedules:**
```json
[
  {
    "id": 1,
    "name": "Hunt Times",
    "startHour": 5,
    "startMinute": 0,
    "endHour": 9,
    "endMinute": 0,
    "days": 65,
    "enabled": true
  }
]
```

| Field | Type | Description |
|-------|------|-------------|
| `id` | integer | Unique schedule ID |
| `name` | string | Display name (max 31 chars) |
| `startHour` | integer | BLE advertising start hour (0-23, UTC) |
| `startMinute` | integer | BLE advertising start minute (0-59) |
| `endHour` | integer | BLE advertising end hour (0-23, UTC) |
| `endMinute` | integer | BLE advertising end minute (0-59) |
| `days` | integer | Bitmask for active days (same as feed schedules) |
| `enabled` | boolean | `true` if this window is active |

---

### Feed Command (UUID: `f33d0015-...`) - Write Only, Requires Auth

**Write (iOS → ESP32) - JSON format:**
```json
{"duration": 5}
```

**Write (iOS → ESP32) - Single byte:**
- Write raw byte value `0x05` for 5 seconds
- Write `0x00` to use default duration from settings

| Field | Type | Description |
|-------|------|-------------|
| `duration` | integer | Feed duration in seconds (0-30, 0 = use default) |

**Notes:**
- Maximum duration is 30 seconds (hardware safety limit)
- Duration of 0 uses `motorDuration` from Settings
- Command is rejected if battery is critical

---

### Feed History (UUID: `f33d0016-...`) - Read Only, Requires Auth

**Read Response (with history):**
```json
[
  {
    "timestamp": 1705936200,
    "duration": 5,
    "manual": false,
    "scheduleName": "Daily at 7:00am"
  },
  {
    "timestamp": 1705849800,
    "duration": 10,
    "manual": true,
    "scheduleName": ""
  }
]
```

**Read Response (empty):**
```json
[]
```

| Field | Type | Description |
|-------|------|-------------|
| `timestamp` | integer | Unix timestamp (seconds since 1970-01-01 UTC) |
| `duration` | integer | Feed duration in seconds |
| `manual` | boolean | `true` if manually triggered, `false` if scheduled |
| `scheduleName` | string | Name of schedule that triggered feed, or empty for manual |

**Notes:**
- History is sorted newest first
- Maximum 50 entries stored

---

### Time Sync (UUID: `f33d0017-...`) - Write Only, Requires Auth

**Write (iOS → ESP32):**
```json
{
  "epoch": 1705936200,
  "tzOffset": -360
}
```

| Field | Type | Description |
|-------|------|-------------|
| `epoch` | integer | Unix timestamp in seconds (UTC) |
| `tzOffset` | integer | Timezone offset in minutes from UTC |

**Timezone Offset Convention (matches Swift's `TimeZone.current.secondsFromGMT() / 60`):**
- **Negative values = West of UTC** (Americas)
- **Positive values = East of UTC** (Europe, Asia)

| Timezone | Offset |
|----------|--------|
| UTC | `0` |
| CST (Central Standard) | `-360` |
| CDT (Central Daylight) | `-300` |
| EST (Eastern Standard) | `-300` |
| EDT (Eastern Daylight) | `-240` |
| PST (Pacific Standard) | `-480` |
| PDT (Pacific Daylight) | `-420` |

**Swift Example:**
```swift
let epoch = Int(Date().timeIntervalSince1970)
let tzOffset = TimeZone.current.secondsFromGMT() / 60
let json = "{\"epoch\":\(epoch),\"tzOffset\":\(tzOffset)}"
```

---

### Set PIN (UUID: `f33d0019-...`) - Write Only, Requires Auth

**Write (iOS → ESP32):**
```json
{"newPin": "5678"}
```

| Field | Type | Description |
|-------|------|-------------|
| `newPin` | string | New 4-6 digit PIN (digits only) |

**Notes:**
- Requires existing authentication (must be unlocked first)
- PIN must be 4-6 digits, numbers only
- To clear PIN, write empty string: `{"newPin": ""}`

---

### WiFi OTA (UUID: `f33d0018-...`) - Write Only, Requires Auth

**Write (iOS → ESP32):**
Write single byte: `0x01` to enable WiFi for OTA update.

**Notes:**
- WiFi AP starts with SSID `FeedMe-XXXX` (device ID)
- Password was generated on first boot (stored in device)
- OTA endpoint: `http://192.168.4.1/update`
- WiFi auto-disables after 5 minutes of inactivity

---

## Security (PIN Protection)

**Requirement:** 4 or 6 digit PIN to access device functions.

**Authentication Flow:**
1. iOS app connects to ESP32 via BLE
2. App reads Device Info (always allowed) - returns `{locked: true, ...}`
3. App prompts user for PIN
4. App writes PIN to Auth characteristic
5. ESP32 validates PIN:
   - Success: Unlocks session, Auth notifies `{success: true}`, all characteristics now accessible
   - Failure: Auth notifies `{success: false, attemptsRemaining: N}`
6. Session remains unlocked until disconnect

**Lockout Policy:**
- 5 failed attempts → 1 minute lockout
- Prevents brute force attacks

**First-Time Setup:**
- Factory default: No PIN set (unlocked)
- App prompts to set PIN on first connection
- Write new PIN to Set PIN characteristic

**PIN Storage:**
- Stored in ESP32 Preferences (NVS), not in plaintext
- SHA-256 hash of PIN + device ID as salt

**Always Accessible (no auth required):**
- Device Info (version, deviceId, locked status)

**Requires Auth:**
- All other characteristics (Status, Settings, Feed Schedules, BLE Schedules, Feed, History, Time Sync, OTA, Set PIN)

---

## Implementation Status

### Phase 1: ESP32 BLE GATT Expansion ✅ COMPLETE
- [x] Add new characteristics to `ble_manager.cpp/h`
- [x] Implement PIN authentication (Auth, Set PIN characteristics)
- [x] Implement read callbacks (Device Info, Status, Settings, Feed Schedules, BLE Schedules, History)
- [x] Implement write callbacks (Settings, Feed Schedules, BLE Schedules, Feed Command, Time Sync, OTA trigger)
- [x] Add session-based auth check (unlock flag per connection)
- [x] Add batteryType to Settings characteristic
- [x] Keep existing WiFi/web interface working during development

### Phase 2: iOS App (New Repo: feedme-ios)
- [ ] Create Xcode project with SwiftUI
- [ ] Implement BluetoothManager with CoreBluetooth
- [ ] Build device scanning and connection UI
- [ ] Implement read operations (status, feed schedules, BLE schedules, history)
- [ ] Implement write operations (settings, feed schedules, BLE schedules, feed, time sync)
- [ ] Test end-to-end with expanded BLE firmware

### Phase 3: OTA Implementation
- [ ] Add WiFi OTA characteristic response with SSID/password
- [ ] Implement OTA flow in iOS app
- [ ] Test firmware updates

### Phase 4: Remove Web Interface (Future)
- [ ] Delete `data/` folder
- [ ] Strip webserver.cpp to OTA-only
- [ ] Simplify radio_manager (no BLE→WiFi wake flow)
- [ ] Final testing

---

## Considerations

### BLE Data Size
- Default MTU: 23 bytes (20 usable)
- iOS typically negotiates larger MTU (up to 512)
- For large payloads: implement chunked read/write if needed

### Reconnection
- iOS app should auto-reconnect to last known device
- Store device identifier in UserDefaults
- Handle connection drops gracefully

### Background Operation
- iOS limits BLE in background
- For notifications (feed completed, low battery): consider local notifications when app reopens
- True push notifications would require internet connectivity

### Battery Life (No Solar Panel)
BLE advertising consumes ~10-15mA. Without a solar panel:
- BLE always on: ~19 days on 7Ah battery
- BLE 12h/day: ~38 days on 7Ah battery
- BLE 6h/day: ~78 days on 7Ah battery

BLE Schedules allow users to define time windows when BLE is active, significantly extending battery life for installations without solar panels.
