# ChadVis Suno Token Helper

Chrome extension for seamless Suno authentication in ChadVis.

## Installation

1. Open Chrome and navigate to `chrome://extensions/`
2. Enable "Developer mode" (toggle in top right)
3. Click "Load unpacked"
4. Select the `browser_extension` folder from ChadVis resources

## Usage

1. Start ChadVis application
2. Navigate to suno.com and login to your account
3. The extension will automatically:
   - Extract your authentication token
   - Send it to ChadVis on port 38945
   - Auto-refresh before token expiry

## Status Indicators

- **Green dot**: Connected to ChadVis, token active
- **Gray dot**: Disconnected (is ChadVis running?)
- **Red dot**: Error occurred

## Manual Refresh

Click the extension icon and use "Refresh Token Now" to manually trigger a token refresh.

## How It Works

1. **Content Script** (`content.js`) - Runs on suno.com, injects page script
2. **Injected Script** (`injected.js`) - Accesses `window.Clerk` in page context
3. **Background Worker** (`background.js`) - Receives tokens, pushes to ChadVis
4. **Token Server** (ChadVis) - Listens on 127.0.0.1:38945

## Security

- Token never leaves localhost (port 38945)
- No external servers involved
- Extension only runs on suno.com
- Source code is fully auditable

## Based On

Originally developed for SunoSync by the same team, adapted for ChadVis.
