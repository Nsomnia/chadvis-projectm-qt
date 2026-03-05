/**
 * SunoSync Token Helper — Injected Script
 * 
 * This script runs in the PAGE context (same world as suno.com's code).
 * It has access to window.Clerk and can call getToken().
 * Communicates with the content script via window.postMessage.
 */

(function () {
    'use strict';

    const MSG_TYPE_TOKEN = 'SUNOSYNC_TOKEN';
    const MSG_TYPE_REFRESH = 'SUNOSYNC_REFRESH';
    const MSG_TYPE_STATUS = 'SUNOSYNC_STATUS';

    /**
     * Wait for window.Clerk to be available, then grab the token.
     * Clerk may take a moment to initialize after page load.
     */
    function waitForClerk(callback, maxAttempts = 30, interval = 1000) {
        let attempts = 0;

        function check() {
            attempts++;

            if (window.Clerk && window.Clerk.session) {
                callback(null, window.Clerk);
                return;
            }

            if (attempts >= maxAttempts) {
                callback(new Error('Clerk not found after ' + maxAttempts + ' attempts'), null);
                return;
            }

            setTimeout(check, interval);
        }

        check();
    }

    /**
     * Grab the current Clerk session token and post it to the content script.
     */
    async function grabToken() {
        try {
            if (!window.Clerk || !window.Clerk.session) {
                window.postMessage({
                    type: MSG_TYPE_STATUS,
                    status: 'no_session',
                    message: 'No Clerk session found. Are you logged in?'
                }, '*');
                return;
            }

            const token = await window.Clerk.session.getToken();

            if (token) {
                window.postMessage({
                    type: MSG_TYPE_TOKEN,
                    token: token,
                    timestamp: Date.now()
                }, '*');
            } else {
                window.postMessage({
                    type: MSG_TYPE_STATUS,
                    status: 'no_token',
                    message: 'Clerk session exists but getToken() returned null. Try refreshing the page.'
                }, '*');
            }
        } catch (err) {
            window.postMessage({
                type: MSG_TYPE_STATUS,
                status: 'error',
                message: 'Error getting token: ' + err.message
            }, '*');
        }
    }

    // Listen for refresh requests from the content script
    window.addEventListener('message', function (event) {
        if (event.source !== window) return;
        if (event.data && event.data.type === MSG_TYPE_REFRESH) {
            grabToken();
        }
    });

    // Initial token grab once Clerk is ready
    waitForClerk(function (err, clerk) {
        if (err) {
            window.postMessage({
                type: MSG_TYPE_STATUS,
                status: 'clerk_not_found',
                message: err.message
            }, '*');
            return;
        }

        // Small delay to ensure session is fully initialized
        setTimeout(grabToken, 500);
    });
})();
