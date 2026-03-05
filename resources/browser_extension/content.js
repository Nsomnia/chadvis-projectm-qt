/**
 * SunoSync Token Helper — Content Script
 * 
 * Runs in the ISOLATED world on suno.com pages.
 * Bridges communication between the page-context injected.js and the
 * background service worker.
 */

(function () {
    'use strict';

    const MSG_TYPE_TOKEN = 'SUNOSYNC_TOKEN';
    const MSG_TYPE_REFRESH = 'SUNOSYNC_REFRESH';
    const MSG_TYPE_STATUS = 'SUNOSYNC_STATUS';

    // --- 1. Inject the page-context script ---
    function injectScript() {
        const script = document.createElement('script');
        script.src = chrome.runtime.getURL('injected.js');
        script.onload = function () {
            this.remove(); // Clean up the <script> tag after execution
        };
        (document.head || document.documentElement).appendChild(script);
    }

    injectScript();

    // --- 2. Listen for messages FROM the injected script (page context) ---
    window.addEventListener('message', function (event) {
        // Only accept messages from the same window
        if (event.source !== window) return;

        const data = event.data;
        if (!data || !data.type) return;

        // Relay token to background service worker
        if (data.type === MSG_TYPE_TOKEN) {
            chrome.runtime.sendMessage({
                action: 'token_received',
                token: data.token,
                timestamp: data.timestamp
            });
        }

        // Relay status updates to background
        if (data.type === MSG_TYPE_STATUS) {
            chrome.runtime.sendMessage({
                action: 'status_update',
                status: data.status,
                message: data.message
            });
        }
    });

    // --- 3. Listen for messages FROM the background service worker ---
    chrome.runtime.onMessage.addListener(function (message, sender, sendResponse) {
        if (message.action === 'refresh_token') {
            // Forward refresh request to the injected script (page context)
            window.postMessage({ type: MSG_TYPE_REFRESH }, '*');
            sendResponse({ ok: true });
        }
        return true; // Keep the message channel open for async response
    });
})();
