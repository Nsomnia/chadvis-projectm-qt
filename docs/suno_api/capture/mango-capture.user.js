// ==UserScript==
// @name         Suno playback capture (redacting, shape-only)
// @namespace    chadvis-recon
// @version      1.0.0
// @description  Records the SHAPE of the encrypted playback channel: the content-rights call, the clip's media_urls entries, and the media URL shape. Never records a token, a key, a clip id, or a complete media URL.
// @match        https://suno.com/*
// @run-at       document-start
// @grant        none
// @sandbox      raw
// @inject-into  page
// ==/UserScript==

(() => {
  'use strict';
  const nativeFetch = window.fetch.bind(window);
  const report = {
    capturedAt: new Date().toISOString(),
    rights: null,
    mediaEntries: [],
    mediaUrlShapes: [],
  };
  const hash = (s) => {
    let h = 0x811c9dc5;
    for (const c of String(s)) {
      h ^= c.charCodeAt(0);
      h = Math.imul(h, 0x01000193) >>> 0;
    }
    return h.toString(16).padStart(8, '0');
  };
  const shape = (url) => {
    try {
      const u = new URL(url, location.href);
      return `${u.host}/${u.pathname.replace(/[0-9a-f-]{36}/gi, '<clip-id>')}`;
    } catch {
      return '<unparseable>';
    }
  };
  const describe = (v) =>
    typeof v === 'string' ? `string(${v.length})` : Array.isArray(v) ? `array(${v.length})` : typeof v;

  // ---- the feed: record media_urls[] shapes only ------------------------
  function scanClips(node, depth = 0) {
    if (!node || depth > 6) return;
    if (Array.isArray(node)) {
      for (const item of node) scanClips(item, depth + 1);
      return;
    }
    if (typeof node !== 'object') return;
    if (Array.isArray(node.media_urls)) {
      for (const m of node.media_urls) {
        report.mediaEntries.push({
          content_type: m.content_type,
          delivery: m.delivery,
          encoding: m.encoding,
          url_shape: shape(m.url),
          has_query: /[?&]/.test(String(m.url || '')),
        });
      }
    }
    for (const v of Object.values(node)) scanClips(v, depth + 1);
  }

  window.fetch = function (input, init) {
    let url = null;
    let rights = false;
    try {
      url = new URL(typeof input === 'string' || input instanceof URL ? String(input) : input.url, location.href);
      rights = url.pathname === '/api/mango/rights';
    } catch { /* observation must never break the page */ }

    const result = nativeFetch(input, init);
    if (rights) {
      const headers = new Headers(
        init?.headers !== undefined ? init.headers : input instanceof Request ? input.headers : undefined);
      const auth = headers.get('Authorization') || '';
      const req = {
        method: (init?.method || (input instanceof Request ? input.method : 'GET')).toUpperCase(),
        host: url.host,
        path: url.pathname,
        headerNames: [...headers.keys()].map((k) => k.toLowerCase()),
        authorization: auth ? `bearer(string(${auth.replace(/^Bearer\s+/i, '').length}))` : 'absent',
        body: null,
      };
      result.then(async (response) => {
        try {
          const copy = response.clone();
          let body = null;
          try {
            const raw = await copy.text();
            const parsed = JSON.parse(raw);
            body = parsed?.content_params ?? parsed?.data?.content_params ?? null;
          } catch { /* body may be unreadable; the shape is enough */ }
          req.body = body
            ? {
                keys: Object.keys(body),
                content_params: Object.fromEntries(
                  Object.entries(body).map(([k, v]) => [k, k === 'content_id' ? `uuid(${hash(v)})` : v])),
              }
            : 'unreadable';
          const copy2 = response.clone();
          const payload = await copy2.json().catch(() => null);
          const data = payload?.data ?? payload;
          report.rights = {
            status: response.status,
            envelopeKeys: payload ? Object.keys(payload) : [],
            fields: data ? Object.fromEntries(Object.entries(data).map(([k, v]) => [k, describe(v)])) : {},
            request: req,
          };
          console.log('[capture] rights captured', report.rights);
        } catch (e) {
          console.log('[capture] rights capture failed', e);
        }
      }).catch(() => {});
    } else if (url && url.protocol === 'https:' && (url.hostname.endsWith('.cloudfront.net') || url.hostname.endsWith('.suno.com'))) {
      const s = shape(url.href);
      if (!report.mediaUrlShapes.includes(s)) report.mediaUrlShapes.push(s);
    }
    return result;
  };

  // ---- print the report --------------------------------------------------
  const dump = () => {
    console.log('[capture] report\n' + JSON.stringify(report, null, 2));
    navigator.clipboard?.writeText(JSON.stringify(report, null, 2))
      .then(() => console.log('[capture] report copied to the clipboard'))
      .catch(() => console.log('[capture] clipboard blocked - copy it from the console'));
  };
  window.__chadvisCaptureReport = () => report;
  window.__chadvisCaptureDump = dump;
  console.log('[capture] installed. Play one song on suno.com, then run __chadvisCaptureDump()');
})();
