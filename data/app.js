// Deer Feeder Timer Web Interface

const API = {
    async get(endpoint) {
        const response = await fetch(`/api${endpoint}`);
        if (!response.ok) {
            const error = await response.json().catch(() => ({}));
            throw new Error(error.error || `HTTP ${response.status}`);
        }
        return response.json();
    },
    async post(endpoint, data = {}) {
        const response = await fetch(`/api${endpoint}`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(data)
        });
        if (!response.ok) {
            const error = await response.json().catch(() => ({}));
            throw new Error(error.error || `HTTP ${response.status}`);
        }
        return response.json();
    }
};

// State
let schedules = [];
let bleSchedules = [];
let settings = {};
let editingScheduleId = null;
let editingBleScheduleId = null;
let heartbeatInterval = null;
let statusInterval = null;
let wifiDisconnected = false;

// Timezone conversion helpers
// Note: getTimezoneOffset() returns positive for behind UTC (e.g., 360 for CST/UTC-6)
function getTimezoneOffset() {
    // Use stored offset from settings if available, otherwise use browser's offset
    return settings.timezoneOffset ?? new Date().getTimezoneOffset();
}

function utcToLocalHour(utcHour) {
    const offset = getTimezoneOffset();
    // Local = UTC - offset/60 (offset is positive for behind UTC)
    const localHour = utcHour - Math.floor(offset / 60);
    return (localHour + 24) % 24;
}

function localToUtcHour(localHour) {
    const offset = getTimezoneOffset();
    // UTC = Local + offset/60
    const utcHour = localHour + Math.floor(offset / 60);
    return (utcHour + 24) % 24;
}

function formatTimeLocal(utcHour, minute) {
    const localHour = utcToLocalHour(utcHour);
    return String(localHour).padStart(2, '0') + ':' + String(minute).padStart(2, '0');
}

// DOM Elements
const elements = {
    connectionStatus: document.getElementById('connection-status'),
    batteryStatus: document.getElementById('battery-status'),
    batteryVoltage: document.getElementById('battery-voltage'),
    chargingIndicator: document.getElementById('charging-indicator'),
    deviceTime: document.getElementById('device-time'),
    nextFeed: document.getElementById('next-feed'),
    locationDisplay: document.getElementById('location-display'),
    vacationBanner: document.getElementById('vacation-banner'),
    throwBtn: document.getElementById('throw-btn'),
    syncTimeBtn: document.getElementById('sync-time-btn'),
    schedulesList: document.getElementById('schedules-list'),
    addScheduleBtn: document.getElementById('add-schedule-btn'),
    motorDuration: document.getElementById('motor-duration'),
    batteryType: document.getElementById('battery-type'),
    detectedVoltage: document.getElementById('detected-voltage'),
    vacationMode: document.getElementById('vacation-mode'),
    deviceVersion: document.getElementById('device-version'),
    deviceId: document.getElementById('device-id'),
    saveSettingsBtn: document.getElementById('save-settings-btn'),
    // Location settings
    settingsLatitude: document.getElementById('settings-latitude'),
    settingsLongitude: document.getElementById('settings-longitude'),
    modal: document.getElementById('schedule-modal'),
    modalTitle: document.getElementById('modal-title'),
    closeModal: document.getElementById('close-modal'),
    scheduleName: document.getElementById('schedule-name'),
    scheduleTime: document.getElementById('schedule-time'),
    scheduleDuration: document.getElementById('schedule-duration'),
    scheduleEnabled: document.getElementById('schedule-enabled'),
    deleteScheduleBtn: document.getElementById('delete-schedule-btn'),
    saveScheduleBtn: document.getElementById('save-schedule-btn'),
    // Schedule type and offset elements
    scheduleType: document.getElementById('schedule-type'),
    scheduleOffset: document.getElementById('schedule-offset'),
    timeGroup: document.getElementById('time-group'),
    offsetGroup: document.getElementById('offset-group'),
    // Season elements
    scheduleStartMonth: document.getElementById('schedule-start-month'),
    scheduleStartDay: document.getElementById('schedule-start-day'),
    scheduleEndMonth: document.getElementById('schedule-end-month'),
    scheduleEndDay: document.getElementById('schedule-end-day'),
    // BLE Schedule elements
    bleSchedulesList: document.getElementById('ble-schedules-list'),
    addBleScheduleBtn: document.getElementById('add-ble-schedule-btn'),
    bleModal: document.getElementById('ble-schedule-modal'),
    bleModalTitle: document.getElementById('ble-modal-title'),
    closeBleModal: document.getElementById('close-ble-modal'),
    bleScheduleName: document.getElementById('ble-schedule-name'),
    bleStartTime: document.getElementById('ble-start-time'),
    bleEndTime: document.getElementById('ble-end-time'),
    bleScheduleEnabled: document.getElementById('ble-schedule-enabled'),
    deleteBleScheduleBtn: document.getElementById('delete-ble-schedule-btn'),
    saveBleScheduleBtn: document.getElementById('save-ble-schedule-btn'),
    toast: document.getElementById('toast'),
    // Feed confirmation modal
    feedModal: document.getElementById('feed-modal'),
    closeFeedModal: document.getElementById('close-feed-modal'),
    cancelFeedBtn: document.getElementById('cancel-feed-btn'),
    confirmFeedBtn: document.getElementById('confirm-feed-btn'),
    feedDuration: document.getElementById('feed-duration')
};

// Initialize
document.addEventListener('DOMContentLoaded', () => {
    setupNavigation();
    setupEventListeners();
    loadData();
    syncTimeOnLoad();

    // Refresh status periodically
    statusInterval = setInterval(loadStatus, 5000);

    // Start heartbeat to keep WiFi alive (every 60 seconds)
    heartbeatInterval = setInterval(sendHeartbeat, 60000);
});

function setupNavigation() {
    document.querySelectorAll('.nav-btn').forEach(btn => {
        btn.addEventListener('click', () => {
            const view = btn.dataset.view;

            // Update nav buttons
            document.querySelectorAll('.nav-btn').forEach(b => b.classList.remove('active'));
            btn.classList.add('active');

            // Update views
            document.querySelectorAll('.view').forEach(v => v.classList.remove('active'));
            document.getElementById(`${view}-view`).classList.add('active');
        });
    });
}

function setupEventListeners() {
    elements.throwBtn.addEventListener('click', openFeedModal);
    elements.syncTimeBtn.addEventListener('click', handleSyncTime);
    elements.addScheduleBtn.addEventListener('click', () => openScheduleModal());
    elements.saveSettingsBtn.addEventListener('click', handleSaveSettings);
    elements.closeModal.addEventListener('click', closeScheduleModal);
    elements.saveScheduleBtn.addEventListener('click', handleSaveSchedule);
    elements.deleteScheduleBtn.addEventListener('click', handleDeleteSchedule);

    // Close modal on backdrop click
    elements.modal.addEventListener('click', (e) => {
        if (e.target === elements.modal) closeScheduleModal();
    });

    // Schedule type change handler (show/hide time vs offset fields)
    elements.scheduleType.addEventListener('change', () => {
        const type = parseInt(elements.scheduleType.value);
        if (type === 0) {
            // Specific time
            elements.timeGroup.classList.remove('hidden');
            elements.offsetGroup.classList.add('hidden');
        } else {
            // Sunrise or Sunset - check if location is set
            if (!settings.locationSet) {
                showToast('Set your location in Settings first', 'error');
                elements.scheduleType.value = '0';  // Reset to Specific Time
                return;
            }
            elements.timeGroup.classList.add('hidden');
            elements.offsetGroup.classList.remove('hidden');
        }
    });

    // BLE Schedule event listeners
    elements.addBleScheduleBtn.addEventListener('click', () => openBleScheduleModal());
    elements.closeBleModal.addEventListener('click', closeBleScheduleModal);
    elements.saveBleScheduleBtn.addEventListener('click', handleSaveBleSchedule);
    elements.deleteBleScheduleBtn.addEventListener('click', handleDeleteBleSchedule);
    elements.bleModal.addEventListener('click', (e) => {
        if (e.target === elements.bleModal) closeBleScheduleModal();
    });

    // Feed confirmation modal
    elements.closeFeedModal.addEventListener('click', closeFeedModal);
    elements.cancelFeedBtn.addEventListener('click', closeFeedModal);
    elements.confirmFeedBtn.addEventListener('click', confirmFeed);
    elements.feedModal.addEventListener('click', (e) => {
        if (e.target === elements.feedModal) closeFeedModal();
    });
}

async function loadData() {
    try {
        await Promise.all([loadStatus(), loadSchedules(), loadBleSchedules(), loadSettings()]);
        elements.connectionStatus.classList.add('connected');
    } catch (error) {
        console.error('Failed to load data:', error);
        elements.connectionStatus.classList.remove('connected');
        showToast('Failed to connect', 'error');
    }
}

async function loadStatus() {
    try {
        const status = await API.get('/status');

        // Battery
        elements.batteryStatus.textContent = status.batteryStatus || '--';
        elements.batteryStatus.className = 'value battery-' + (status.batteryStatus || '').toLowerCase();
        elements.batteryVoltage.textContent = status.batteryVoltage ?
            status.batteryVoltage.toFixed(1) + 'V' : '--V';

        if (status.isCharging) {
            elements.chargingIndicator.classList.remove('hidden');
        } else {
            elements.chargingIndicator.classList.add('hidden');
        }

        // Time
        if (status.currentTime) {
            const date = new Date(status.currentTime);
            elements.deviceTime.textContent = date.toLocaleTimeString([], {
                hour: '2-digit',
                minute: '2-digit'
            });
        } else {
            elements.deviceTime.textContent = '--:--';
        }

        // Next feed
        elements.nextFeed.textContent = status.nextFeed || '--:--';

        // Vacation mode
        if (status.vacationMode) {
            elements.vacationBanner.classList.remove('hidden');
        } else {
            elements.vacationBanner.classList.add('hidden');
        }

        // WiFi timeout warning
        const warningBanner = document.getElementById('wifi-timeout-warning');
        const countdown = document.getElementById('wifi-countdown');
        if (status.wifiRemainingSeconds !== undefined && status.wifiRemainingSeconds <= 180) {
            // Show warning when 3 minutes or less remaining
            const mins = Math.floor(status.wifiRemainingSeconds / 60);
            const secs = status.wifiRemainingSeconds % 60;
            countdown.textContent = `${mins}:${secs.toString().padStart(2, '0')}`;
            warningBanner.classList.remove('hidden');
        } else {
            warningBanner.classList.add('hidden');
        }

        // Diagnostics
        const diagUptime = document.getElementById('diag-uptime');
        const diagHeap = document.getElementById('diag-heap');
        const diagWifiClients = document.getElementById('diag-wifi-clients');
        const diagResetReason = document.getElementById('diag-reset-reason');

        if (diagUptime && status.uptime !== undefined) {
            const hours = Math.floor(status.uptime / 3600);
            const mins = Math.floor((status.uptime % 3600) / 60);
            diagUptime.textContent = hours > 0 ? `${hours}h ${mins}m` : `${mins}m`;
        }
        if (diagHeap && status.freeHeap !== undefined) {
            diagHeap.textContent = `${Math.round(status.freeHeap / 1024)} KB`;
        }
        if (diagWifiClients && status.wifiClients !== undefined) {
            diagWifiClients.textContent = status.wifiClients;
        }
        if (diagResetReason && status.lastResetReason) {
            diagResetReason.textContent = status.lastResetReason;
        }

        elements.connectionStatus.classList.add('connected');
    } catch (error) {
        console.error('Status load error:', error);
        elements.connectionStatus.classList.remove('connected');
        handleWifiDisconnect();
    }
}

async function loadSchedules() {
    try {
        schedules = await API.get('/schedules');
        renderSchedules();
    } catch (error) {
        console.error('Schedules load error:', error);
    }
}

function renderSchedules() {
    if (schedules.length === 0) {
        elements.schedulesList.innerHTML = `
            <div class="empty-state">
                <p>No schedules yet</p>
                <p>Tap "Add Schedule" to create one</p>
            </div>
        `;
        return;
    }

    elements.schedulesList.innerHTML = schedules.map(s => {
        const daysText = formatDays(s.days);
        const timeText = formatScheduleTime(s);
        return `
            <div class="schedule-item ${s.enabled ? '' : 'disabled'}" data-id="${s.id}">
                <div class="schedule-info">
                    <div class="schedule-name">${s.name || 'Schedule'}</div>
                    <div class="schedule-details">${timeText} - ${daysText}</div>
                </div>
                <label class="toggle schedule-toggle" onclick="event.stopPropagation()">
                    <input type="checkbox" ${s.enabled ? 'checked' : ''}
                           onchange="toggleSchedule(${s.id}, this.checked)">
                    <span class="toggle-slider"></span>
                </label>
            </div>
        `;
    }).join('');

    // Add click handlers for editing
    document.querySelectorAll('.schedule-item').forEach(item => {
        item.addEventListener('click', () => {
            const id = parseInt(item.dataset.id);
            const schedule = schedules.find(s => s.id === id);
            if (schedule) openScheduleModal(schedule);
        });
    });
}

function formatDays(daysBitmask) {
    const days = ['Su', 'M', 'Tu', 'W', 'Th', 'F', 'Sa'];
    if (daysBitmask === 0x7F) return 'Daily';
    if (daysBitmask === 0x3E) return 'Weekdays';
    if (daysBitmask === 0x41) return 'Weekends';

    let result = [];
    for (let i = 0; i < 7; i++) {
        if (daysBitmask & (1 << i)) {
            result.push(days[i]);
        }
    }
    return result.join('/');
}

function formatScheduleTime(schedule) {
    // Schedule type: 0=specific time, 1=sunrise, 2=sunset
    const type = schedule.scheduleType ?? 0;
    if (type === 0) {
        return formatTimeLocal(schedule.hour, schedule.minute);
    }

    const offset = schedule.sunOffset ?? 0;
    let label = type === 1 ? 'Sunrise' : 'Sunset';

    if (offset !== 0) {
        const sign = offset > 0 ? '+' : '';
        label += ` ${sign}${offset}m`;
    }
    return label;
}

async function loadSettings() {
    try {
        settings = await API.get('/settings');
        elements.motorDuration.value = settings.motorDuration || 5;
        elements.batteryType.value = settings.batteryType ?? 0;  // Default to SLA (0)
        elements.detectedVoltage.textContent = settings.detectedVoltage || '--';
        elements.vacationMode.checked = settings.vacationMode || false;
        elements.deviceVersion.textContent = settings.version || '--';
        elements.deviceId.textContent = settings.deviceId || '----';

        // Location settings
        if (settings.locationSet && settings.latitude !== undefined) {
            elements.settingsLatitude.value = settings.latitude.toFixed(4);
            elements.settingsLongitude.value = settings.longitude.toFixed(4);
            elements.locationDisplay.textContent = `${settings.latitude.toFixed(2)}, ${settings.longitude.toFixed(2)}`;
        } else {
            elements.settingsLatitude.value = '';
            elements.settingsLongitude.value = '';
            elements.locationDisplay.textContent = 'Unknown';
        }

        // Load feed history when settings are loaded
        loadFeedHistory();
    } catch (error) {
        console.error('Settings load error:', error);
    }
}

async function loadFeedHistory() {
    try {
        const history = await API.get('/feed-history');
        renderFeedHistory(history);
    } catch (error) {
        console.error('Feed history load error:', error);
        const historyList = document.getElementById('feed-history-list');
        if (historyList) {
            historyList.innerHTML = '<div class="empty-state">Failed to load history</div>';
        }
    }
}

function renderFeedHistory(history) {
    const historyList = document.getElementById('feed-history-list');
    if (!historyList) return;

    if (!history || history.length === 0) {
        historyList.innerHTML = '<div class="empty-state">No feed history yet</div>';
        return;
    }

    historyList.innerHTML = history.map(event => {
        const date = new Date(event.timestamp * 1000);
        const timeStr = date.toLocaleString([], {
            month: 'short',
            day: 'numeric',
            hour: '2-digit',
            minute: '2-digit'
        });
        const source = event.manual ? 'Manual' : (event.scheduleName || 'Schedule');
        return `
            <div class="history-item">
                <span class="history-time">${timeStr}</span>
                <span class="history-source">${source}</span>
                <span class="history-duration">${event.duration}s</span>
            </div>
        `;
    }).join('');
}

function openFeedModal() {
    // Show current motor duration in the confirmation modal
    const duration = settings.motorDuration || 5;
    elements.feedDuration.textContent = duration;
    elements.feedModal.classList.remove('hidden');
}

function closeFeedModal() {
    elements.feedModal.classList.add('hidden');
}

async function confirmFeed() {
    closeFeedModal();

    try {
        elements.throwBtn.disabled = true;
        elements.throwBtn.textContent = 'Feeding...';

        await API.post('/throw');
        showToast('Feed command sent!', 'success');
    } catch (error) {
        console.error('Throw error:', error);
        showToast(error.message || 'Failed to send command', 'error');
    } finally {
        elements.throwBtn.disabled = false;
        elements.throwBtn.innerHTML = '<span class="btn-icon">&#9654;</span> Feed Now';
    }
}

// Get user's location (returns null if denied or unavailable)
async function getLocation() {
    return new Promise((resolve) => {
        if (!navigator.geolocation) {
            console.log('Geolocation not supported');
            resolve(null);
            return;
        }
        navigator.geolocation.getCurrentPosition(
            (position) => {
                resolve({
                    latitude: position.coords.latitude,
                    longitude: position.coords.longitude
                });
            },
            (error) => {
                console.log('Geolocation error:', error.message);
                resolve(null);
            },
            { timeout: 10000, maximumAge: 300000 }  // 10s timeout, cache for 5 min
        );
    });
}

async function handleSyncTime() {
    try {
        const epoch = Math.floor(Date.now() / 1000);
        const offset = new Date().getTimezoneOffset();  // Minutes behind UTC
        const location = await getLocation();

        const data = { epoch, offset };
        if (location) {
            data.latitude = location.latitude;
            data.longitude = location.longitude;
        }

        await API.post('/time', data);
        showToast(location ? 'Time & location synced!' : 'Time synced!', 'success');
        loadStatus();
    } catch (error) {
        console.error('Time sync error:', error);
        showToast('Failed to sync time', 'error');
    }
}

function syncTimeOnLoad() {
    // Auto-sync time and location on page load
    setTimeout(async () => {
        try {
            const epoch = Math.floor(Date.now() / 1000);
            const offset = new Date().getTimezoneOffset();  // Minutes behind UTC
            const location = await getLocation();

            const data = { epoch, offset };
            if (location) {
                data.latitude = location.latitude;
                data.longitude = location.longitude;
            }

            await API.post('/time', data);
            console.log('Auto time sync complete' + (location ? ' (with location)' : ''));
        } catch (error) {
            console.error('Auto time sync failed:', error);
        }
    }, 1000);
}

async function handleSaveSettings() {
    try {
        // Validate and clamp motor duration to 1-30 seconds
        let duration = parseInt(elements.motorDuration.value) || 5;
        duration = Math.max(1, Math.min(30, duration));
        elements.motorDuration.value = duration;  // Update UI to show clamped value

        const data = {
            motorDuration: duration,
            batteryType: parseInt(elements.batteryType.value),
            vacationMode: elements.vacationMode.checked
        };

        // Include location if provided
        const lat = parseFloat(elements.settingsLatitude.value);
        const lon = parseFloat(elements.settingsLongitude.value);
        if (!isNaN(lat) && !isNaN(lon) && lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180) {
            data.latitude = lat;
            data.longitude = lon;
        }

        await API.post('/settings', data);
        showToast('Settings saved!', 'success');

        // Reload settings to update location status
        await loadSettings();
        loadStatus();
    } catch (error) {
        console.error('Save settings error:', error);
        showToast('Failed to save settings', 'error');
    }
}

function openScheduleModal(schedule = null) {
    editingScheduleId = schedule ? schedule.id : null;

    elements.modalTitle.textContent = schedule ? 'Edit Schedule' : 'Add Schedule';
    elements.deleteScheduleBtn.classList.toggle('hidden', !schedule);

    // Populate form - use settings.motorDuration as default for new schedules
    const defaultDuration = settings.motorDuration || 5;
    elements.scheduleName.value = schedule?.name || '';
    // Convert UTC time to local for display in time picker
    const localTime = schedule ? formatTimeLocal(schedule.hour, schedule.minute) : '06:00';
    elements.scheduleTime.value = localTime;
    elements.scheduleDuration.value = schedule?.duration || defaultDuration;
    elements.scheduleEnabled.checked = schedule?.enabled ?? true;

    // Schedule type (0=specific time, 1=sunrise, 2=sunset)
    const scheduleType = schedule?.scheduleType ?? 0;
    elements.scheduleType.value = scheduleType;
    elements.scheduleOffset.value = schedule?.sunOffset ?? 0;

    // Show/hide time vs offset fields based on schedule type
    if (scheduleType === 0) {
        elements.timeGroup.classList.remove('hidden');
        elements.offsetGroup.classList.add('hidden');
    } else {
        elements.timeGroup.classList.add('hidden');
        elements.offsetGroup.classList.remove('hidden');
    }

    // Seasonal date fields
    elements.scheduleStartMonth.value = schedule?.startMonth ?? -1;
    elements.scheduleStartDay.value = schedule?.startDay ?? 1;
    elements.scheduleEndMonth.value = schedule?.endMonth ?? -1;
    elements.scheduleEndDay.value = schedule?.endDay ?? 31;

    // Days checkboxes
    const days = schedule?.days ?? 0x3E; // Default to weekdays
    document.querySelectorAll('#feed-day-selector input').forEach(cb => {
        const day = parseInt(cb.dataset.day);
        cb.checked = (days & (1 << day)) !== 0;
    });

    elements.modal.classList.remove('hidden');
}

function closeScheduleModal() {
    elements.modal.classList.add('hidden');
    editingScheduleId = null;
}

async function handleSaveSchedule() {
    try {
        // Collect days bitmask
        let days = 0;
        document.querySelectorAll('#feed-day-selector input').forEach(cb => {
            if (cb.checked) {
                days |= (1 << parseInt(cb.dataset.day));
            }
        });

        const [localHours, minutes] = elements.scheduleTime.value.split(':').map(Number);
        // Convert local time to UTC for storage
        const utcHours = localToUtcHour(localHours);

        // Validate and clamp duration to 1-30 seconds
        const defaultDuration = settings.motorDuration || 5;
        let duration = parseInt(elements.scheduleDuration.value) || defaultDuration;
        duration = Math.max(1, Math.min(30, duration));

        // Schedule type and offset
        const scheduleType = parseInt(elements.scheduleType.value);
        let sunOffset = parseInt(elements.scheduleOffset.value) || 0;
        sunOffset = Math.max(-120, Math.min(120, sunOffset));

        // Validate location for sunrise/sunset schedules
        if (scheduleType !== 0 && !settings.locationSet) {
            showToast('Set your location in Settings first', 'error');
            return;
        }

        // Limit to 1 sunrise and 1 sunset schedule
        if (scheduleType !== 0) {
            const existingSchedule = schedules.find(s =>
                s.scheduleType === scheduleType && s.id !== editingScheduleId
            );
            if (existingSchedule) {
                const typeName = scheduleType === 1 ? 'sunrise' : 'sunset';
                showToast(`Only one ${typeName} schedule allowed`, 'error');
                return;
            }
        }

        // Seasonal date fields
        const startMonth = parseInt(elements.scheduleStartMonth.value);
        const startDay = parseInt(elements.scheduleStartDay.value) || 1;
        const endMonth = parseInt(elements.scheduleEndMonth.value);
        const endDay = parseInt(elements.scheduleEndDay.value) || 31;

        const data = {
            name: elements.scheduleName.value || '',
            hour: utcHours,
            minute: minutes,
            days: days,
            duration: duration,
            enabled: elements.scheduleEnabled.checked,
            scheduleType: scheduleType,
            sunOffset: sunOffset,
            startMonth: startMonth,
            startDay: startDay,
            endMonth: endMonth,
            endDay: endDay
        };

        if (editingScheduleId) {
            // Update existing
            await API.post(`/schedules/update?id=${editingScheduleId}`, data);
            showToast('Schedule updated!', 'success');
        } else {
            // Create new
            await API.post('/schedules', data);
            showToast('Schedule created!', 'success');
        }

        closeScheduleModal();
        loadSchedules();
    } catch (error) {
        console.error('Save schedule error:', error);
        showToast('Failed to save schedule', 'error');
    }
}

async function handleDeleteSchedule() {
    if (!editingScheduleId) return;

    if (!confirm('Delete this schedule?')) return;

    try {
        await API.post(`/schedules/delete?id=${editingScheduleId}`);
        showToast('Schedule deleted', 'success');
        closeScheduleModal();
        loadSchedules();
    } catch (error) {
        console.error('Delete schedule error:', error);
        showToast('Failed to delete schedule', 'error');
    }
}

async function toggleSchedule(id, enabled) {
    try {
        const schedule = schedules.find(s => s.id === id);
        if (!schedule) return;

        await API.post(`/schedules/update?id=${id}`, { ...schedule, enabled });
        loadSchedules();
    } catch (error) {
        console.error('Toggle schedule error:', error);
        showToast('Failed to update schedule', 'error');
        loadSchedules(); // Reload to reset toggle state
    }
}

function showToast(message, type = 'success') {
    elements.toast.textContent = message;
    elements.toast.className = `toast ${type}`;

    setTimeout(() => {
        elements.toast.classList.add('hidden');
    }, 3000);
}

// Heartbeat to keep WiFi alive
async function sendHeartbeat() {
    if (wifiDisconnected) return;

    try {
        await fetch('/api/heartbeat');
    } catch (error) {
        console.error('Heartbeat failed:', error);
        handleWifiDisconnect();
    }
}

function handleWifiDisconnect() {
    if (wifiDisconnected) return;

    wifiDisconnected = true;

    // Stop all intervals to prevent repeated errors
    if (heartbeatInterval) {
        clearInterval(heartbeatInterval);
        heartbeatInterval = null;
    }
    if (statusInterval) {
        clearInterval(statusInterval);
        statusInterval = null;
    }

    // Hide the countdown warning if visible
    const warningBanner = document.getElementById('wifi-timeout-warning');
    if (warningBanner) {
        warningBanner.classList.add('hidden');
    }

    // Show disconnect message
    const banner = document.getElementById('wifi-timeout-banner');
    if (banner) {
        banner.classList.remove('hidden');
    }
    elements.connectionStatus.classList.remove('connected');
    showToast('WiFi was disabled. Use BLE to reconnect.', 'error');
}

function handleKeepAlive() {
    sendHeartbeat();
    showToast('WiFi timer reset!', 'success');
}

// BLE Schedules
async function loadBleSchedules() {
    try {
        bleSchedules = await API.get('/ble-schedules');
        renderBleSchedules();
    } catch (error) {
        console.error('BLE schedules load error:', error);
    }
}

function renderBleSchedules() {
    if (bleSchedules.length === 0) {
        elements.bleSchedulesList.innerHTML = `
            <div class="empty-state">
                <p>No BLE schedules yet</p>
                <p>Tap "Add BLE Schedule" to create one</p>
            </div>
        `;
        return;
    }

    elements.bleSchedulesList.innerHTML = bleSchedules.map(s => {
        const daysText = formatDays(s.days);
        const localStartTime = formatTimeLocal(s.startHour, s.startMinute);
        const localEndTime = formatTimeLocal(s.endHour, s.endMinute);
        return `
            <div class="schedule-item ${s.enabled ? '' : 'disabled'}" data-ble-id="${s.id}">
                <div class="schedule-info">
                    <div class="schedule-name">${s.name || 'BLE Window'}</div>
                    <div class="schedule-details">${localStartTime} - ${localEndTime} - ${daysText}</div>
                </div>
                <label class="toggle schedule-toggle" onclick="event.stopPropagation()">
                    <input type="checkbox" ${s.enabled ? 'checked' : ''}
                           onchange="toggleBleSchedule(${s.id}, this.checked)">
                    <span class="toggle-slider"></span>
                </label>
            </div>
        `;
    }).join('');

    // Add click handlers for editing
    document.querySelectorAll('.schedule-item[data-ble-id]').forEach(item => {
        item.addEventListener('click', () => {
            const id = parseInt(item.dataset.bleId);
            const schedule = bleSchedules.find(s => s.id === id);
            if (schedule) openBleScheduleModal(schedule);
        });
    });
}

function openBleScheduleModal(schedule = null) {
    editingBleScheduleId = schedule ? schedule.id : null;

    elements.bleModalTitle.textContent = schedule ? 'Edit BLE Schedule' : 'Add BLE Schedule';
    elements.deleteBleScheduleBtn.classList.toggle('hidden', !schedule);

    // Populate form
    elements.bleScheduleName.value = schedule?.name || '';
    // Convert UTC times to local for display in time pickers
    const localStartTime = schedule ? formatTimeLocal(schedule.startHour, schedule.startMinute) : '06:00';
    const localEndTime = schedule ? formatTimeLocal(schedule.endHour, schedule.endMinute) : '08:00';
    elements.bleStartTime.value = localStartTime;
    elements.bleEndTime.value = localEndTime;
    elements.bleScheduleEnabled.checked = schedule?.enabled ?? true;

    // Days checkboxes
    const days = schedule?.days ?? 0x7F; // Default to daily
    document.querySelectorAll('#ble-day-selector input').forEach(cb => {
        const day = parseInt(cb.dataset.day);
        cb.checked = (days & (1 << day)) !== 0;
    });

    elements.bleModal.classList.remove('hidden');
}

function closeBleScheduleModal() {
    elements.bleModal.classList.add('hidden');
    editingBleScheduleId = null;
}

async function handleSaveBleSchedule() {
    try {
        // Collect days bitmask
        let days = 0;
        document.querySelectorAll('#ble-day-selector input').forEach(cb => {
            if (cb.checked) {
                days |= (1 << parseInt(cb.dataset.day));
            }
        });

        const [localStartHours, startMinutes] = elements.bleStartTime.value.split(':').map(Number);
        const [localEndHours, endMinutes] = elements.bleEndTime.value.split(':').map(Number);
        // Convert local times to UTC for storage
        const utcStartHours = localToUtcHour(localStartHours);
        const utcEndHours = localToUtcHour(localEndHours);

        const data = {
            name: elements.bleScheduleName.value || 'BLE Window',
            startHour: utcStartHours,
            startMinute: startMinutes,
            endHour: utcEndHours,
            endMinute: endMinutes,
            days: days,
            enabled: elements.bleScheduleEnabled.checked
        };

        if (editingBleScheduleId) {
            await API.post(`/ble-schedules/update?id=${editingBleScheduleId}`, data);
            showToast('BLE schedule updated!', 'success');
        } else {
            await API.post('/ble-schedules', data);
            showToast('BLE schedule created!', 'success');
        }

        closeBleScheduleModal();
        loadBleSchedules();
    } catch (error) {
        console.error('Save BLE schedule error:', error);
        showToast('Failed to save BLE schedule', 'error');
    }
}

async function handleDeleteBleSchedule() {
    if (!editingBleScheduleId) return;

    if (!confirm('Delete this BLE schedule?')) return;

    try {
        await API.post(`/ble-schedules/delete?id=${editingBleScheduleId}`);
        showToast('BLE schedule deleted', 'success');
        closeBleScheduleModal();
        loadBleSchedules();
    } catch (error) {
        console.error('Delete BLE schedule error:', error);
        showToast('Failed to delete BLE schedule', 'error');
    }
}

async function toggleBleSchedule(id, enabled) {
    try {
        const schedule = bleSchedules.find(s => s.id === id);
        if (!schedule) return;

        await API.post(`/ble-schedules/update?id=${id}`, { ...schedule, enabled });
        loadBleSchedules();
    } catch (error) {
        console.error('Toggle BLE schedule error:', error);
        showToast('Failed to update BLE schedule', 'error');
        loadBleSchedules(); // Reload to reset toggle state
    }
}

// OTA Firmware Update
function setupOTA() {
    const fileInput = document.getElementById('ota-file');
    const selectBtn = document.getElementById('ota-select-btn');
    const fileInfo = document.getElementById('ota-file-info');
    const filename = document.getElementById('ota-filename');
    const uploadBtn = document.getElementById('ota-upload-btn');
    const progress = document.getElementById('ota-progress');
    const progressFill = document.getElementById('ota-progress-fill');
    const progressText = document.getElementById('ota-progress-text');
    const status = document.getElementById('ota-status');

    if (!fileInput || !selectBtn) return;

    selectBtn.addEventListener('click', () => fileInput.click());

    fileInput.addEventListener('change', () => {
        const file = fileInput.files[0];
        if (file) {
            filename.textContent = `${file.name} (${(file.size / 1024).toFixed(1)} KB)`;
            fileInfo.classList.remove('hidden');
        } else {
            fileInfo.classList.add('hidden');
        }
    });

    uploadBtn.addEventListener('click', async () => {
        const file = fileInput.files[0];
        if (!file) return;

        // Hide file info, show progress
        fileInfo.classList.add('hidden');
        selectBtn.classList.add('hidden');
        progress.classList.remove('hidden');
        status.classList.add('hidden');

        const formData = new FormData();
        formData.append('firmware', file);

        const xhr = new XMLHttpRequest();
        xhr.open('POST', '/api/ota');

        xhr.upload.onprogress = (e) => {
            if (e.lengthComputable) {
                const pct = Math.round((e.loaded / e.total) * 100);
                progressFill.style.width = pct + '%';
                progressText.textContent = pct + '%';
            }
        };

        xhr.onload = () => {
            progress.classList.add('hidden');
            status.classList.remove('hidden');
            if (xhr.status === 200) {
                status.textContent = 'Update successful! Rebooting...';
                status.className = 'ota-status success';
                showToast('Firmware updated! Reconnect in a few seconds.', 'success');
            } else {
                status.textContent = 'Update failed. Please try again.';
                status.className = 'ota-status error';
                selectBtn.classList.remove('hidden');
                showToast('Firmware update failed', 'error');
            }
        };

        xhr.onerror = () => {
            progress.classList.add('hidden');
            status.classList.remove('hidden');
            status.textContent = 'Upload error. Please try again.';
            status.className = 'ota-status error';
            selectBtn.classList.remove('hidden');
            showToast('Upload failed', 'error');
        };

        xhr.send(formData);
    });
}

// Initialize OTA on page load
document.addEventListener('DOMContentLoaded', setupOTA);
