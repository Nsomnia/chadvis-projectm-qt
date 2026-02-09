(function() {
    'use strict';
    
    // Stealth Script to mock standard browser properties and hide automation signals
    // Injected at DocumentCreation time
    
    const mockPlugins = [
        { name: "Chrome PDF Plugin", filename: "internal-pdf-viewer", description: "Portable Document Format" },
        { name: "Chrome PDF Viewer", filename: "mhjfbmdgcfjbbpaeojofohoefgiehjai", description: "Portable Document Format" },
        { name: "Native Client", filename: "internal-nacl-plugin", description: "" }
    ];

    const mockMimeTypes = [
        { type: "application/pdf", suffixes: "pdf", description: "Portable Document Format", enabledPlugin: mockPlugins[0] },
        { type: "application/x-google-chrome-pdf", suffixes: "pdf", description: "Portable Document Format", enabledPlugin: mockPlugins[1] },
        { type: "application/x-nacl", suffixes: "", description: "Native Client Executable", enabledPlugin: mockPlugins[2] },
        { type: "application/x-pnacl", suffixes: "", description: "Portable Native Client Executable", enabledPlugin: mockPlugins[2] }
    ];

    // Mock Navigator Plugins
    Object.defineProperty(navigator, 'plugins', {
        get: () => {
            const p = mockPlugins;
            p.item = function(index) { return this[index]; };
            p.namedItem = function(name) { return this.find(x => x.name === name); };
            p.refresh = function() {};
            return p;
        }
    });

    // Mock Navigator MimeTypes
    Object.defineProperty(navigator, 'mimeTypes', {
        get: () => {
            const m = mockMimeTypes;
            m.item = function(index) { return this[index]; };
            m.namedItem = function(name) { return this.find(x => x.type === name); };
            return m;
        }
    });

    // Hide WebDriver
    Object.defineProperty(navigator, 'webdriver', { get: () => undefined });
    
    // Mock Languages
    Object.defineProperty(navigator, 'languages', { get: () => ['en-US', 'en'] });
    
    // Mock Permissions (often checked)
    const originalQuery = window.navigator.permissions.query;
    window.navigator.permissions.query = (parameters) => (
        parameters.name === 'notifications' ?
        Promise.resolve({ state: Notification.permission }) :
        originalQuery(parameters)
    );
    
    // Mock window.chrome
    if (!window.chrome) {
        window.chrome = {
            runtime: {},
            loadTimes: function() {},
            csi: function() {},
            app: {
                isInstalled: false,
                InstallState: {
                    DISABLED: "disabled",
                    INSTALLED: "installed",
                    NOT_INSTALLED: "not_installed"
                },
                RunningState: {
                    CANNOT_RUN: "cannot_run",
                    READY_TO_RUN: "ready_to_run",
                    RUNNING: "running"
                }
            }
        };
    }
    
    // Add WebGL Fingerprint Noise (optional, good for advanced checks)
    const getParameter = WebGLRenderingContext.prototype.getParameter;
    WebGLRenderingContext.prototype.getParameter = function(parameter) {
        // UNMASKED_VENDOR_WEBGL
        if (parameter === 37445) {
            return 'Google Inc. (NVIDIA)';
        }
        // UNMASKED_RENDERER_WEBGL
        if (parameter === 37446) {
            return 'ANGLE (NVIDIA, NVIDIA GeForce RTX 3060 Direct3D11 vs_5_0 ps_5_0, D3D11)';
        }
        return getParameter(parameter);
    };

    console.log("Stealth script injected successfully.");
})();
