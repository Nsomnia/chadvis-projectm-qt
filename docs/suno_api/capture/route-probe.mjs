// Redacting route probe for the API-base question (T0333 / T0336).
//
// Usage:  node route-probe.mjs
//
// Answers one question with the app's own credential: does a route answer on the
// base this client uses today (`studio-api-prod.suno.com/api`) and/or on the base
// the browser now uses (`suno.com/a/api`)? It POSTs the same bodies the client's
// own services build, and prints SHAPES only: status, content-type, top-level key
// names, array names with their lengths, and the key names of the first array
// element. It never prints a value, a token, a cookie, an id or a URL.
//
// The bearer is the one this app already stored in the OS keychain -- the same
// read `mango-probe.mjs --bearer` performs. Nothing is written to disk.
//
// Requires Node >= 18. No npm packages.

import { execFileSync } from 'node:child_process';

const KEYCHAIN_SERVICE = 'chadvis-projectm-qt';
const KEYCHAIN_ACCOUNT = 'chadvis-projectm-qt/chadvis/chadvis-projectm-qt/chadvis/suno/bearer';

const bearer = execFileSync(
  'security',
  ['find-generic-password', '-s', KEYCHAIN_SERVICE, '-a', KEYCHAIN_ACCOUNT, '-w'],
  { encoding: 'utf8' }).trim();

if (!bearer) {
  console.error('no bearer in the keychain; sign in first');
  process.exit(2);
}

const OLD_BASE = 'https://studio-api-prod.suno.com/api';
const NEW_BASE = 'https://suno.com/a/api';

const probes = [
  { label: 'explore  (old base)', method: 'POST', url: `${OLD_BASE}/unified/explore`, body: { cursor: null } },
  { label: 'explore  (new base)', method: 'POST', url: `${NEW_BASE}/unified/explore`, body: { cursor: null } },
  { label: 'feed/v3  (old base)', method: 'POST', url: `${OLD_BASE}/feed/v3`, body: { cursor: null, limit: 20 } },
  { label: 'feed/v3  (new base)', method: 'POST', url: `${NEW_BASE}/feed/v3`, body: { cursor: null, limit: 20 } },
  { label: 'notice/v3 (new base)', method: 'GET', url: `${NEW_BASE}/notification/v3?include_hooks=true`, body: null },
];

const kindOf = (value) => {
  if (value === null) return 'null';
  if (Array.isArray(value)) return `array(${value.length})`;
  return typeof value;
};

const shapeOf = (value) => {
  if (Array.isArray(value)) {
    const first = value.find((entry) => entry && typeof entry === 'object');
    return `[${value.length}]${first ? ` first keys: ${Object.keys(first).join(', ')}` : ''}`;
  }
  if (value && typeof value === 'object') return `object keys: ${Object.keys(value).join(', ')}`;
  return typeof value; // never the value itself
};

for (const probe of probes) {
  const headers = { Authorization: `Bearer ${bearer}`, Accept: 'application/json' };
  if (probe.method === 'POST') headers['Content-Type'] = 'application/json';

  let response;
  let text = '';
  try {
    response = await fetch(probe.url, {
      method: probe.method,
      headers,
      body: probe.body === null ? undefined : JSON.stringify(probe.body),
    });
    text = await response.text();
  } catch (error) {
    console.log(`\n${probe.label}\n  transport error: ${error.message}`);
    continue;
  }

  const host = new URL(probe.url).host;
  console.log(`\n${probe.label}`);
  console.log(`  ${probe.method} ${host}${new URL(probe.url).pathname}`);
  console.log(`  status: ${response.status}  content-type: ${response.headers.get('content-type') ?? '<none>'}  bytes: ${text.length}`);

  let parsed = null;
  try {
    parsed = JSON.parse(text);
  } catch {
    console.log('  body: not JSON (first 80 bytes redacted)');
    continue;
  }

  for (const [key, value] of Object.entries(parsed)) {
    console.log(`  ${key}: ${kindOf(value)}${Array.isArray(value) || (value && typeof value === 'object') ? ` -- ${shapeOf(value)}` : ''}`);
  }

  // The one field that says whether this page is a real page: a cursor or a count.
  for (const key of ['next_cursor', 'has_more', 'item_count', 'notified_at', 'detail', 'error']) {
    if (key in parsed) {
      const value = parsed[key];
      console.log(`  ${key}: present (${value === null ? 'null' : typeof value}${typeof value === 'string' ? `, ${value.length} chars` : ''})`);
    }
  }
}
