/**
 * ChadVis Suno Token Helper — Background Service Worker
 *
 * Receives tokens from the content script, pushes them to the
 * ChadVis Qt app via HTTP, and manages the auto-refresh timer.
 */

const TOKEN_SERVER_URL = 'http://127.0.0.1:38945';
const REFRESH_INTERVAL_SECONDS = 50;
const ALARM_NAME = 'chadvis_token_refresh';

// --- State ---
let state = {
    lastToken: null,
    lastRefresh: null,
    appConnected: false,
    sunoLoggedIn: false,
    lastError: null
};

// --- Persist & Load State ---
function saveState() {
    chrome.storage.local.set({ chadvis_state: state });
}

function loadState() {
    chrome.storage.local.get('chadvis_state', (result) => {
        if (result.chadvis_state) {
            state = { ...state, ...result.chadvis_state };
        }
    });
}

loadState();

// --- JWT Helpers ---
function parseJwt(token) {
    try {
        const base64Url = token.split('.')[1];
        const base64 = base64Url.replace(/-/g, '+').replace(/_/g, '/');
        const jsonPayload = decodeURIComponent(atob(base64).split('').map(function (c) {
            return '%' + ('00' + c.charCodeAt(0).toString(16)).slice(-2);
        }).join(''));
        return JSON.parse(jsonPayload);
    } catch (e) {
        return null;
    }
}

// --- Send token to ChadVis app ---
async function pushTokenToApp(token) {
    try {
        const response = await fetch(TOKEN_SERVER_URL + '/token', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ token: token })
        });

        if (response.ok) {
            state.appConnected = true;
            state.lastError = null;
            saveState();
            updateBadge('connected');

            scheduleSmartRefresh(token);

            return true;
        } else {
            state.appConnected = false;
            state.lastError = 'App returned HTTP ' + response.status;
            saveState();
            updateBadge('error');
            return false;
        }
    } catch (err) {
        state.appConnected = false;
        state.lastError = 'Cannot reach ChadVis app. Is it running?';
        saveState();
        updateBadge('disconnected');
        return false;
    }
}

// --- Scheduling Logic ---
function scheduleSmartRefresh(token) {
    // Clear existing
    chrome.alarms.clear(ALARM_NAME);

    let delayInMinutes = REFRESH_INTERVAL_SECONDS / 60; // Fallback default

    if (token) {
        const claims = parseJwt(token);
        if (claims && claims.exp) {
            const now = Math.floor(Date.now() / 1000);
            const timeToExpiry = claims.exp - now;

            if (timeToExpiry > 30) {
                // Refresh 30 seconds before expiry
                const refreshInSeconds = timeToExpiry - 30;
                delayInMinutes = refreshInSeconds / 60;
                // Cap minimum delay to avoid rapid loops if something is wrong
                if (delayInMinutes < 0.1) delayInMinutes = 0.1;

                console.log(`[SunoSync] Token expires in ${timeToExpiry}s. Scheduling refresh in ${refreshInSeconds}s.`);
            } else {
                // Expiring very soon, refresh immediately (or very short delay)
                delayInMinutes = 0.1;
            }
        }
    }

    chrome.alarms.create(ALARM_NAME, {
        delayInMinutes: delayInMinutes
    });
}

// --- Check if app is running (Polling) ---
async function checkAppStatus() {
    try {
        const response = await fetch(TOKEN_SERVER_URL + '/status', {
            method: 'GET',
            signal: AbortSignal.timeout(3000)
        });

        const wasConnected = state.appConnected;

        if (response.ok) {
            state.appConnected = true;
            state.lastError = null;

            // If just connected (or re-connected), immediately push token
            if (!wasConnected && state.lastToken) {
                console.log("[SunoSync] App discovered! Pushing cached token...");
                pushTokenToApp(state.lastToken);
            }
        } else {
            state.appConnected = false;
        }
    } catch {
        state.appConnected = false;
    }
    saveState();
    updateBadge(state.appConnected ? 'connected' : 'disconnected');
}

// --- Update extension badge ---
function updateBadge(status) {
    const colors = {
        connected: '#10b981',    // Green
        disconnected: '#6b7280', // Gray
        error: '#ef4444'         // Red
    };
    const texts = {
        connected: '✓',
        disconnected: '',
        error: '!'
    };

    chrome.action.setBadgeBackgroundColor({ color: colors[status] || '#6b7280' });
    chrome.action.setBadgeText({ text: texts[status] || '' });
}

// --- Request token refresh from active suno.com tab ---
async function requestTokenRefresh() {
    try {
        const tabs = await chrome.tabs.query({
            url: ['https://suno.com/*', 'https://*.suno.com/*']
        });

        if (tabs.length === 0) {
            state.sunoLoggedIn = false;
            state.lastError = 'No suno.com tab open';
            saveState();
            return;
        }

        // Send refresh request to the first matching tab
        for (const tab of tabs) {
            try {
                await chrome.tabs.sendMessage(tab.id, { action: 'refresh_token' });
                return; // Success, only need one tab
            } catch {
                // Tab might not have content script loaded yet
                continue;
            }
        }
    } catch (err) {
        console.error('SunoSync: Error requesting refresh:', err);
    }
}

// --- Message handler ---
chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
    if (message.action === 'token_received') {
        state.lastToken = message.token;
        state.lastRefresh = message.timestamp || Date.now();
        state.sunoLoggedIn = true;
        saveState();

        // Push to app
        pushTokenToApp(message.token).then((success) => {
            sendResponse({ success: success });
        });

        // Note: Smart Schedule is handled inside pushTokenToApp
        return true;
    }

    if (message.action === 'status_update') {
        if (message.status === 'no_session' || message.status === 'clerk_not_found') {
            state.sunoLoggedIn = false;
        }
        state.lastError = message.message;
        saveState();
    }

    if (message.action === 'get_state') {
        sendResponse(state);
        return false;
    }

    if (message.action === 'manual_refresh') {
        requestTokenRefresh();
        sendResponse({ ok: true });
        return false;
    }

    if (message.action === 'check_app') {
        checkAppStatus().then(() => sendResponse(state));
        return true;
    }
});

// --- Alarm handler (auto-refresh) ---
chrome.alarms.onAlarm.addListener((alarm) => {
    if (alarm.name === ALARM_NAME) {
        requestTokenRefresh();
    }

    if (alarm.name === 'app_poll') {
        checkAppStatus();
    }
});

// --- On install/startup ---
chrome.runtime.onInstalled.addListener(() => {
    // Start the polling alarm
    chrome.alarms.create('app_poll', {
        periodInMinutes: 5 / 60 // Every 5 seconds
    });

    checkAppStatus();
});

chrome.runtime.onStartup.addListener(() => {
    chrome.alarms.create('app_poll', {
        periodInMinutes: 5 / 60 // Every 5 seconds
    });

    loadState();
    checkAppStatus();
});
