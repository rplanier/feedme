# FeedMe - Smart Deer Feeder Timer

A battery-efficient, drop-in replacement timer for Texas Hunter 12V and 6V deer feeders with smartphone control and e-paper display.

## Features

- **Scheduled Feeding**: Set multiple daily feed times with day-of-week selection
- **Smartphone Control**: Configure via web app over direct WiFi hotspot
- **E-Paper Display**: View status and trigger manual feeds without your phone
- **Battery Monitoring**: Track voltage and charging status for 6V or 12V batteries
- **Solar Charger Support**: Dedicated input for solar panels with built-in charge regulators
- **Vacation Mode**: Pause all feeding schedules with one tap
- **Feed History**: Log of recent feed events (scheduled and manual)
- **Drop-In Replacement**: Wiring harness uses the same connectors as the original timer

## Design Philosophy: Battery First

FeedMe is designed for remote deployment where the battery may go months between charges. Every design decision prioritizes battery life.

### Hardware Choices

- **XIAO ESP32-C6**: Lower power consumption than other ESP32 MCUs, with better BLE range via coded PHY
- **E-Paper Display**: Zero power draw when static (image persists without power)
- **DS3231M RTC**: Dedicated timekeeping chip with coin cell backup, allowing the main MCU to sleep

### Software Architecture

- **BLE-First Connectivity**: WiFi draws ~100mA; BLE advertising uses ~1mA. After initial setup, the feeder advertises via BLE and only enables WiFi when you explicitly connect.
- **Scheduled BLE Windows**: Limit BLE advertising to specific hours (e.g., 6-9 AM when you might visit) for maximum battery savings. When no BLE schedules exist, BLE stays on continuously.
- **Auto WiFi Timeout**: WiFi shuts off after 5 minutes of inactivity, returning to low-power BLE mode.
- **Deep Sleep**: When idle, the ESP32 enters deep sleep and wakes only for scheduled feeds or button presses.

---

## Quick Start

### 1. Power On

Connect your 6V or 12V battery to the feeder. The display shows the FeedMe splash screen while booting, then transitions to the Overview screen with current time, battery status, and next scheduled feed.

> [!IMPORTANT]
> On first setup, the display may show "Time not synced." Connect via WiFi and open the web app to sync the time automatically.
>
> Feed schedules will **not** execute until the time is synchronized.

### 2. Connect to WiFi

**WiFi is automatically enabled on power-up** for easy initial setup. Press the button to reach the **Connectivity** screen, which shows:

- **WiFi Status**: "On" (should show this on first boot)
- **SSID**: `FeedMe-XXXX` (your unique network name)
- **Password**: An 8-character password
- **QR Code**: Scan to connect automatically

**Easiest method - Scan the QR Code:**
1. Open your phone's camera app
2. Point it at the QR code on the display
3. Tap the notification to join the network
4. Your browser will automatically open the setup page (may take up to 30 seconds)

**Manual method:**
1. Go to Settings → WiFi on your phone
2. Connect to `FeedMe-XXXX`
3. Enter the password shown on the display
4. Open a browser and navigate to `http://192.168.4.1`

> [!TIP]
> The captive portal popup may not appear on all devices. If it doesn't appear within 30 seconds, use the manual method above.

> [!NOTE]
> WiFi turns off after 5 minutes of inactivity to save battery. The web interface shows a countdown timer.

### 3. Sync Time

When you open the web interface, the time syncs automatically from your phone. You'll see the time update on both the web interface and e-paper display.

You can also tap **Sync Time** in the web interface to sync manually.

> [!IMPORTANT]
> The feeder needs the correct, synchronized time to run schedules.

### 4. Create Feed Schedules

1. In the web interface, go to the **Feed Schedules** tab
2. Tap **Add Schedule**
3. Configure:
   - **Time**: When to dispense feed (in your local time)
   - **Days**: Which days of the week (tap to toggle, or use "Daily")
   - **Duration**: How long the motor runs (1-30 seconds, default 5)
   - **Enabled**: Toggle on/off without deleting the schedule
4. Tap **Save**

> [!TIP]
> Start with a shorter duration (3-5 seconds) and adjust based on how much feed is dispensed. The default duration can be changed in **Settings**.

### 5. Test the Motor

Before leaving your feeder unattended, verify the motor works.

**From the web interface:**
1. Go to the **Overview** tab
2. Tap **Feed Now**
3. Confirm the duration and tap **Confirm**

**From the display (no phone needed):**
1. Navigate to the **Overview** screen
2. **Hold the button** for 2 seconds
3. A "STAND BACK!" warning appears
4. A **15-second countdown** begins
5. Press the button anytime to **cancel**
6. After the countdown, the motor runs for 5 seconds (or your configured default)

> [!WARNING]
> The feeder motor can throw corn with considerable force, especially 12V motors. Always stand clear before triggering a feed.

### 6. Deploy

Once configured:
1. Close the web interface (or just walk away)
2. WiFi turns off after 5 minutes, switching to low-power BLE mode
3. Schedules run automatically at the configured times
4. After 60 seconds of inactivity, the display enters screensaver mode

---

## Using the Display

### Screen Navigation

The display has 5 screens. **Press the button briefly** to cycle through them:

| Screen | What It Shows |
|--------|---------------|
| **Overview** | Time, date, battery status, next scheduled feed |
| **Connectivity** | WiFi status, network name, password, QR code |
| **Feed Schedules** | List of feeding schedules |
| **BLE Schedules** | BLE advertising windows |
| **About** | Firmware version, device ID |

### Button Actions

| Action | Result |
|--------|--------|
| **Press** (short tap) | Go to next screen |
| **Hold** (2+ seconds) | Context action (see below) |

**Hold actions by screen:**
- **Overview**: Start manual feed (15-second countdown)
- **Connectivity**: Toggle WiFi on/off
- **Feed Schedules**: Enter scroll mode (if more than 4 schedules)
- **BLE Schedules**: Enter scroll mode (if more than 4 schedules)
- **About**: No action

### Status Icons

The Overview screen header shows:
- **Battery**: Fill level indicates charge (100% / 75% / 50% / 25%)
- **WiFi**: Icon when on, X when off
- **BLE**: Bluetooth symbol when advertising

### Screensaver

After 60 seconds without button presses, the display shows the FeedMe logo. Press any button to wake and return to the Overview screen.

---

## Power Management

### Battery Status

The device auto-detects whether you have a 6V or 12V battery. Thresholds differ by battery type:

**12V Battery (6-cell lead-acid)**

| Status | Voltage | Meaning |
|--------|---------|---------|
| **Good** | ≥12.4V | ~75%+ charge |
| **Fair** | ≥12.0V | ~25%+ charge |
| **Low** | ≥11.5V | Depleted, charge soon |
| **Critical** | <10.8V | Risk of damage, feeds disabled |

**6V Battery (3-cell lead-acid)**

| Status | Voltage | Meaning |
|--------|---------|---------|
| **Good** | ≥6.2V | ~75%+ charge |
| **Fair** | ≥6.0V | ~25%+ charge |
| **Low** | ≥5.75V | Depleted, charge soon |
| **Critical** | <5.4V | Risk of damage, feeds disabled |

> [!NOTE]
> Manual feed tests still work at critical battery levels so you can always verify the motor.

### Solar Panel Support

FeedMe has a dedicated solar input with its own voltage monitoring. When a panel is connected and producing power:

- **"CHG" indicator** appears on the display and web interface
- Charging is detected when solar voltage exceeds 5V
- The solar circuit is separate from battery sensing for accurate detection

**Setup**: Connect a solar panel that matches your battery voltage (6V panel for 6V battery, 12V panel for 12V battery). The panel charges the battery during daylight, extending time between manual charges.

---

## Advanced Features

### BLE Schedules

By default, BLE advertising is always on. For maximum battery savings, you can restrict BLE to specific time windows:

1. In the web interface, go to the **BLE Schedules** tab
2. Tap **Add BLE Schedule**
3. Set start time, end time, and days
4. Tap **Save**

**Example**: Create a window from 5:00 AM to 10:00 AM if you typically check feeders in the morning. Outside this window, BLE is off and the feeder is not discoverable—but feed schedules still run normally.

**To restore always-on BLE**: Delete all BLE schedules.

### Vacation Mode

Pause all feeding without deleting your schedules:

1. Open the web interface
2. Go to **Settings**
3. Toggle **Vacation Mode** on

The display shows a "VACATION MODE" banner. Turn it off to resume normal feeding.

---

## Reconnecting Later

After WiFi auto-disables, you have two options:

**Option 1: From the display**
1. Press button to wake from screensaver (if needed)
2. Navigate to the **Connectivity** screen
3. **Hold the button** to turn WiFi on
4. Connect your phone as before

**Option 2: Via BLE app**
1. Use [LightBlue](https://apps.apple.com/app/lightblue/id557428110) (iOS) or [nRF Connect](https://play.google.com/store/apps/details?id=no.nordicsemi.android.mcp) (Android)
2. Find `FeedMe-XXXX` in the device list
3. Connect and write any value to the wake characteristic
4. WiFi will turn on; connect as before

---

## Settings

Access via the **Settings** tab in the web interface:

| Setting | Default | Range | Description |
|---------|---------|-------|-------------|
| **Motor Duration** | 5 sec | 1-30 sec | Default motor run time for scheduled feeds |
| **Vacation Mode** | Off | On/Off | Pause all scheduled feeds |

> [!NOTE]
> Individual schedules can override the default motor duration.

---

## Troubleshooting

### Can't find the WiFi network

- **Check power**: Is the display on? If not, check battery connections or charge the battery.
- **Check WiFi status**: Navigate to Connectivity screen—does it show "WiFi: On"?
- **Enable WiFi**: If WiFi is off, hold the button to turn it on.
- **Move closer**: WiFi range is approximately 150-300 feet with clear line of sight.
- **Check BLE schedules**: WiFi only auto-enables on cold boot, not when exiting a BLE window.

### Time shows "Not synced"

- Open the web interface—time syncs automatically on page load
- Ensure your phone has the correct time and timezone
- Tap **Sync Time** to sync manually

### Schedules not running

- **Time not synced**: Check for the warning banner
- **Vacation Mode**: Check for the "VACATION MODE" banner
- **Critical battery**: Feeds are disabled to protect the battery
- **Schedule disabled**: Verify the schedule is enabled and set for today

### Motor doesn't run

- **Test manually**: Try "Feed Now" from web interface or hold button on Overview
- **Check connections**: Verify motor wires are secure
- **Check battery**: Very low voltage may not provide enough current
- **Check motor**: If the display shows feeding activity but nothing happens, the motor may be faulty

### WiFi keeps turning off

This is intentional to save battery. WiFi turns off after 5 minutes of inactivity. To stay connected:
- Keep the web interface open
- Interact with the page occasionally
- The page sends a keepalive signal every 60 seconds

### Display not responding to buttons

- **Wait for refresh**: E-paper takes 1-2 seconds to refresh; button presses during refresh are queued
- **Press firmly**: The button requires a definite press
- **Wake first**: If the screensaver is showing, press to wake before navigating

---

## Technical Specifications

| Component | Details |
|-----------|---------|
| **MCU** | Seeed XIAO ESP32-C6 |
| **Display** | Waveshare 2.9" e-Paper, 296×128 pixels |
| **RTC** | DS3231M with CR2032 backup |
| **Power Input** | 6V or 12V SLA, AGM or Gel battery |
| **Motor Control** | P-channel MOSFET, fail-safe OFF when unpowered |
| **WiFi** | 2.4GHz, WPA2, 5-minute auto-timeout |
| **BLE** | Bluetooth Low Energy advertising |

---

## Safety Features

- **Motor Fail-Safe**: If the ESP32 crashes or loses power, the motor turns OFF (not on)
- **Watchdog Timer**: Firmware hangs trigger automatic reset after 30 seconds
- **Battery Protection**: Feeds disabled at critical voltage (10.8V / 5.4V) to prevent damage
- **Feed Countdown**: 15-second warning before manual feeds, with cancel option
- **Maximum Duration**: Motor runs limited to 30 seconds per feed event
