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
let wifiSchedules = [];
let settings = {};
let editingScheduleId = null;
let editingWifiScheduleId = null;

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
    // WiFi Schedule elements
    wifiSchedulesList: document.getElementById('wifi-schedules-list'),
    addWifiScheduleBtn: document.getElementById('add-wifi-schedule-btn'),
    wifiModal: document.getElementById('wifi-schedule-modal'),
    wifiModalTitle: document.getElementById('wifi-modal-title'),
    closeWifiModal: document.getElementById('close-wifi-modal'),
    wifiScheduleName: document.getElementById('wifi-schedule-name'),
    wifiStartTime: document.getElementById('wifi-start-time'),
    wifiEndTime: document.getElementById('wifi-end-time'),
    wifiScheduleEnabled: document.getElementById('wifi-schedule-enabled'),
    deleteWifiScheduleBtn: document.getElementById('delete-wifi-schedule-btn'),
    saveWifiScheduleBtn: document.getElementById('save-wifi-schedule-btn'),
    toast: document.getElementById('toast')
};

// Initialize
document.addEventListener('DOMContentLoaded', () => {
    setupNavigation();
    setupEventListeners();
    loadData();
    syncTimeOnLoad();

    // Refresh status periodically
    setInterval(loadStatus, 5000);
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
    elements.throwBtn.addEventListener('click', handleThrow);
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

    // WiFi Schedule event listeners
    elements.addWifiScheduleBtn.addEventListener('click', () => openWifiScheduleModal());
    elements.closeWifiModal.addEventListener('click', closeWifiScheduleModal);
    elements.saveWifiScheduleBtn.addEventListener('click', handleSaveWifiSchedule);
    elements.deleteWifiScheduleBtn.addEventListener('click', handleDeleteWifiSchedule);
    elements.wifiModal.addEventListener('click', (e) => {
        if (e.target === elements.wifiModal) closeWifiScheduleModal();
    });
}

async function loadData() {
    try {
        await Promise.all([loadStatus(), loadSchedules(), loadWifiSchedules(), loadSettings()]);
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

        elements.connectionStatus.classList.add('connected');
    } catch (error) {
        console.error('Status load error:', error);
        elements.connectionStatus.classList.remove('connected');
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

async function handleThrow() {
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
        const data = {
            motorDuration: parseInt(elements.motorDuration.value) || 5,
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

    // Populate form
    elements.scheduleName.value = schedule?.name || '';
    elements.scheduleTime.value = schedule?.time || '06:00';
    elements.scheduleDuration.value = schedule?.duration || 0;
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

        const data = {
            name: elements.scheduleName.value || '',
            hour: hours,
            minute: minutes,
            days: days,
            duration: parseInt(elements.scheduleDuration.value) || 0,
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

// WiFi Schedules
async function loadWifiSchedules() {
    try {
        wifiSchedules = await API.get('/wifi-schedules');
        renderWifiSchedules();
    } catch (error) {
        console.error('WiFi schedules load error:', error);
    }
}

function renderWifiSchedules() {
    if (wifiSchedules.length === 0) {
        elements.wifiSchedulesList.innerHTML = `
            <div class="empty-state">
                <p>No WiFi schedules yet</p>
                <p>Tap "Add WiFi Schedule" to create one</p>
            </div>
        `;
        return;
    }

    elements.wifiSchedulesList.innerHTML = wifiSchedules.map(s => {
        const daysText = formatDays(s.days);
        return `
            <div class="schedule-item ${s.enabled ? '' : 'disabled'}" data-wifi-id="${s.id}">
                <div class="schedule-info">
                    <div class="schedule-name">${s.name || 'WiFi Window'}</div>
                    <div class="schedule-details">${s.startTime} - ${s.endTime} - ${daysText}</div>
                </div>
                <label class="toggle schedule-toggle" onclick="event.stopPropagation()">
                    <input type="checkbox" ${s.enabled ? 'checked' : ''}
                           onchange="toggleWifiSchedule(${s.id}, this.checked)">
                    <span class="toggle-slider"></span>
                </label>
            </div>
        `;
    }).join('');

    // Add click handlers for editing
    document.querySelectorAll('.schedule-item[data-wifi-id]').forEach(item => {
        item.addEventListener('click', () => {
            const id = parseInt(item.dataset.wifiId);
            const schedule = wifiSchedules.find(s => s.id === id);
            if (schedule) openWifiScheduleModal(schedule);
        });
    });
}

function openWifiScheduleModal(schedule = null) {
    editingWifiScheduleId = schedule ? schedule.id : null;

    elements.wifiModalTitle.textContent = schedule ? 'Edit WiFi Schedule' : 'Add WiFi Schedule';
    elements.deleteWifiScheduleBtn.classList.toggle('hidden', !schedule);

    // Populate form
    elements.wifiScheduleName.value = schedule?.name || '';
    elements.wifiStartTime.value = schedule?.startTime || '06:00';
    elements.wifiEndTime.value = schedule?.endTime || '08:00';
    elements.wifiScheduleEnabled.checked = schedule?.enabled ?? true;

    // Days checkboxes
    const days = schedule?.days ?? 0x7F; // Default to daily
    document.querySelectorAll('#wifi-day-selector input').forEach(cb => {
        const day = parseInt(cb.dataset.day);
        cb.checked = (days & (1 << day)) !== 0;
    });

    elements.wifiModal.classList.remove('hidden');
}

function closeWifiScheduleModal() {
    elements.wifiModal.classList.add('hidden');
    editingWifiScheduleId = null;
}

async function handleSaveWifiSchedule() {
    try {
        // Collect days bitmask
        let days = 0;
        document.querySelectorAll('#wifi-day-selector input').forEach(cb => {
            if (cb.checked) {
                days |= (1 << parseInt(cb.dataset.day));
            }
        });

        const [startHours, startMinutes] = elements.wifiStartTime.value.split(':').map(Number);
        const [endHours, endMinutes] = elements.wifiEndTime.value.split(':').map(Number);

        const data = {
            name: elements.wifiScheduleName.value || 'WiFi Window',
            startHour: startHours,
            startMinute: startMinutes,
            endHour: endHours,
            endMinute: endMinutes,
            days: days,
            enabled: elements.wifiScheduleEnabled.checked
        };

        if (editingWifiScheduleId) {
            await API.post(`/wifi-schedules/update?id=${editingWifiScheduleId}`, data);
            showToast('WiFi schedule updated!', 'success');
        } else {
            await API.post('/wifi-schedules', data);
            showToast('WiFi schedule created!', 'success');
        }

        closeWifiScheduleModal();
        loadWifiSchedules();
    } catch (error) {
        console.error('Save WiFi schedule error:', error);
        showToast('Failed to save WiFi schedule', 'error');
    }
}

async function handleDeleteWifiSchedule() {
    if (!editingWifiScheduleId) return;

    if (!confirm('Delete this WiFi schedule?')) return;

    try {
        await API.post(`/wifi-schedules/delete?id=${editingWifiScheduleId}`);
        showToast('WiFi schedule deleted', 'success');
        closeWifiScheduleModal();
        loadWifiSchedules();
    } catch (error) {
        console.error('Delete WiFi schedule error:', error);
        showToast('Failed to delete WiFi schedule', 'error');
    }
}

async function toggleWifiSchedule(id, enabled) {
    try {
        const schedule = wifiSchedules.find(s => s.id === id);
        if (!schedule) return;

        await API.post(`/wifi-schedules/update?id=${id}`, { ...schedule, enabled });
        loadWifiSchedules();
    } catch (error) {
        console.error('Toggle WiFi schedule error:', error);
        showToast('Failed to update WiFi schedule', 'error');
        loadWifiSchedules(); // Reload to reset toggle state
    }
}
