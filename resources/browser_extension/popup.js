/**
 * SunoSync Token Helper — Popup Script
 * 
 * Reads state from the background service worker and updates the popup UI.
 */

(function () {
    'use strict';

    const appDot = document.getElementById('app-dot');
    const appText = document.getElementById('app-text');
    const sunoDot = document.getElementById('suno-dot');
    const sunoText = document.getElementById('suno-text');
    const lastRefresh = document.getElementById('last-refresh');
    const errorBox = document.getElementById('error-box');
    const refreshBtn = document.getElementById('refresh-btn');
    const checkBtn = document.getElementById('check-btn');

    function updateUI(state) {
        // App connection status
        if (state.appConnected) {
            appDot.className = 'dot green';
            appText.textContent = 'Connected';
        } else {
            appDot.className = 'dot red';
            appText.textContent = 'Disconnected';
        }

        // Suno login status
        if (state.sunoLoggedIn) {
            sunoDot.className = 'dot green';
            sunoText.textContent = 'Logged In';
        } else {
            sunoDot.className = 'dot gray';
            sunoText.textContent = 'Not Detected';
        }

        // Last refresh
        if (state.lastRefresh) {
            const date = new Date(state.lastRefresh);
            const now = new Date();
            const diffSec = Math.floor((now - date) / 1000);

            if (diffSec < 60) {
                lastRefresh.textContent = diffSec + 's ago';
            } else if (diffSec < 3600) {
                lastRefresh.textContent = Math.floor(diffSec / 60) + 'm ago';
            } else {
                lastRefresh.textContent = date.toLocaleTimeString();
            }
        } else {
            lastRefresh.textContent = '—';
        }

        // Error
        if (state.lastError && !state.appConnected) {
            errorBox.textContent = state.lastError;
            errorBox.style.display = 'block';
        } else {
            errorBox.style.display = 'none';
        }
    }

    // Fetch current state from background
    function refreshState() {
        chrome.runtime.sendMessage({ action: 'get_state' }, (response) => {
            if (response) {
                updateUI(response);
            }
        });
    }

    // Manual refresh button
    refreshBtn.addEventListener('click', () => {
        refreshBtn.textContent = 'Refreshing...';
        refreshBtn.disabled = true;

        chrome.runtime.sendMessage({ action: 'manual_refresh' }, () => {
            setTimeout(() => {
                refreshBtn.textContent = 'Refresh Token Now';
                refreshBtn.disabled = false;
                refreshState();
            }, 2000);
        });
    });

    // Check connection button
    checkBtn.addEventListener('click', () => {
        checkBtn.textContent = 'Checking...';
        checkBtn.disabled = true;

        chrome.runtime.sendMessage({ action: 'check_app' }, (response) => {
            if (response) {
                updateUI(response);
            }
            checkBtn.textContent = 'Check Connection';
            checkBtn.disabled = false;
        });
    });

    // Initial load
    refreshState();

    // Auto-refresh popup every 5 seconds while open
    setInterval(refreshState, 5000);
})();
