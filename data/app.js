// Deer Feeder Timer Web Interface

const API = {
    async get(endpoint) {
        const response = await fetch(`/api${endpoint}`);
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        return response.json();
    },
    async post(endpoint, data = {}) {
        const response = await fetch(`/api${endpoint}`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(data)
        });
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
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

// DOM Elements
const elements = {
    appTitle: document.getElementById('app-title'),
    connectionStatus: document.getElementById('connection-status'),
    batteryStatus: document.getElementById('battery-status'),
    batteryVoltage: document.getElementById('battery-voltage'),
    chargingIndicator: document.getElementById('charging-indicator'),
    deviceTime: document.getElementById('device-time'),
    nextFeed: document.getElementById('next-feed'),
    vacationBanner: document.getElementById('vacation-banner'),
    throwBtn: document.getElementById('throw-btn'),
    syncTimeBtn: document.getElementById('sync-time-btn'),
    schedulesList: document.getElementById('schedules-list'),
    addScheduleBtn: document.getElementById('add-schedule-btn'),
    motorDuration: document.getElementById('motor-duration'),
    vacationMode: document.getElementById('vacation-mode'),
    deviceId: document.getElementById('device-id'),
    saveSettingsBtn: document.getElementById('save-settings-btn'),
    modal: document.getElementById('schedule-modal'),
    modalTitle: document.getElementById('modal-title'),
    closeModal: document.getElementById('close-modal'),
    scheduleName: document.getElementById('schedule-name'),
    scheduleTime: document.getElementById('schedule-time'),
    scheduleDuration: document.getElementById('schedule-duration'),
    scheduleEnabled: document.getElementById('schedule-enabled'),
    deleteScheduleBtn: document.getElementById('delete-schedule-btn'),
    saveScheduleBtn: document.getElementById('save-schedule-btn'),
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

        // Update title with version
        if (status.version) {
            const versionedTitle = `FeedMe v${status.version}`;
            elements.appTitle.textContent = versionedTitle;
            document.title = versionedTitle;
        }

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
        return `
            <div class="schedule-item ${s.enabled ? '' : 'disabled'}" data-id="${s.id}">
                <div class="schedule-info">
                    <div class="schedule-name">${s.name || 'Schedule'}</div>
                    <div class="schedule-details">${s.time} - ${daysText}</div>
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

async function loadSettings() {
    try {
        settings = await API.get('/settings');
        elements.motorDuration.value = settings.motorDuration || 5;
        elements.vacationMode.checked = settings.vacationMode || false;
        elements.deviceId.textContent = settings.deviceId || '----';
    } catch (error) {
        console.error('Settings load error:', error);
    }
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
        showToast('Failed to send command', 'error');
    } finally {
        elements.throwBtn.disabled = false;
        elements.throwBtn.innerHTML = '<span class="btn-icon">&#9654;</span> Feed Now';
    }
}

async function handleSyncTime() {
    try {
        const epoch = Math.floor(Date.now() / 1000);
        const offset = new Date().getTimezoneOffset();  // Minutes behind UTC
        await API.post('/time', { epoch, offset });
        showToast('Time synced!', 'success');
        loadStatus();
    } catch (error) {
        console.error('Time sync error:', error);
        showToast('Failed to sync time', 'error');
    }
}

function syncTimeOnLoad() {
    // Auto-sync time on page load
    setTimeout(async () => {
        try {
            const epoch = Math.floor(Date.now() / 1000);
            const offset = new Date().getTimezoneOffset();  // Minutes behind UTC
            await API.post('/time', { epoch, offset });
            console.log('Auto time sync complete');
        } catch (error) {
            console.error('Auto time sync failed:', error);
        }
    }, 1000);
}

async function handleSaveSettings() {
    try {
        // Validate and clamp motor duration to 2-15 seconds
        let duration = parseInt(elements.motorDuration.value) || 5;
        duration = Math.max(2, Math.min(15, duration));
        elements.motorDuration.value = duration;  // Update UI to show clamped value

        const data = {
            motorDuration: duration,
            vacationMode: elements.vacationMode.checked
        };

        await API.post('/settings', data);
        showToast('Settings saved!', 'success');
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
    elements.scheduleTime.value = schedule?.time || '06:00';
    elements.scheduleDuration.value = schedule?.duration || defaultDuration;
    elements.scheduleEnabled.checked = schedule?.enabled ?? true;

    // Days checkboxes
    const days = schedule?.days ?? 0x3E; // Default to weekdays
    document.querySelectorAll('.day-selector input').forEach(cb => {
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
        document.querySelectorAll('.day-selector input').forEach(cb => {
            if (cb.checked) {
                days |= (1 << parseInt(cb.dataset.day));
            }
        });

        const [hours, minutes] = elements.scheduleTime.value.split(':').map(Number);

        // Validate and clamp duration to 2-15 seconds
        const defaultDuration = settings.motorDuration || 5;
        let duration = parseInt(elements.scheduleDuration.value) || defaultDuration;
        duration = Math.max(2, Math.min(15, duration));

        const data = {
            name: elements.scheduleName.value || '',
            hour: hours,
            minute: minutes,
            days: days,
            duration: duration,
            enabled: elements.scheduleEnabled.checked,
            startMonth: -1,
            startDay: -1,
            endMonth: -1,
            endDay: -1
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
        return `
            <div class="schedule-item ${s.enabled ? '' : 'disabled'}" data-ble-id="${s.id}">
                <div class="schedule-info">
                    <div class="schedule-name">${s.name || 'BLE Window'}</div>
                    <div class="schedule-details">${s.startTime} - ${s.endTime} - ${daysText}</div>
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
    elements.bleStartTime.value = schedule?.startTime || '06:00';
    elements.bleEndTime.value = schedule?.endTime || '08:00';
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

        const [startHours, startMinutes] = elements.bleStartTime.value.split(':').map(Number);
        const [endHours, endMinutes] = elements.bleEndTime.value.split(':').map(Number);

        const data = {
            name: elements.bleScheduleName.value || 'BLE Window',
            startHour: startHours,
            startMinute: startMinutes,
            endHour: endHours,
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
