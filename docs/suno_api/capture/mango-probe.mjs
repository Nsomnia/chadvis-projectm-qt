// Redacting capture probe for the encrypted ("mango") playback channel.
//
// Usage:  node mango-probe.mjs [clipId]
//         node mango-probe.mjs --bearer        (authenticated variant)
//
//   - With no argument it takes the first clip that has a media_urls[] entry
//     from this machine's own Suno library database. That is the client's own
//     captured feed data, so the probe needs no login and no network session
//     beyond the two calls below.
//   - --bearer reads the bearer this app already has in the OS keychain
//     (macOS service `chadvis-projectm-qt`, account
//     `chadvis-projectm-qt/chadvis/chadvis-projectm-qt/chadvis/suno/bearer`)
//     and sends it, which captures the *authenticated* variant of the call.
//   - It prints SHAPES only: field names, value lengths, host names, path
//     templates. It never prints a key, an IV, a token, a clip id, or a
//     complete media URL.
//
// Requires Node >= 18 (node:crypto webcrypto). No npm packages.

import { webcrypto } from 'node:crypto';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import os from 'node:os';
import path from 'node:path';
import fs from 'node:fs';

const subtle = webcrypto.subtle;
const enc = new TextEncoder();

const RIGHTS_URL = 'https://studio-api-prod.suno.com/api/mango/rights';
const dbPath = path.join(
  os.homedir(),
  'Library/Application Support/chadvis-projectm-qt/suno_library.db');

const useBearer = process.argv.includes('--bearer');
const bearer = useBearer
  ? execFileSync('security',
      ['find-generic-password', '-s', 'chadvis-projectm-qt',
       '-a', 'chadvis-projectm-qt/chadvis/chadvis-projectm-qt/chadvis/suno/bearer', '-w'],
      { encoding: 'utf8' }).trim()
  : '';
if (useBearer) console.log('bearer length    :', bearer.length, '(from the OS keychain)');

const sha = (s) => createHash('sha256').update(s).digest('hex').slice(0, 12);
const shape = (url) => {
  const u = new URL(url);
  return `${u.host}${u.pathname.replace(/[0-9a-f-]{36}/gi, '<clip-id>')}`;
};
const describe = (v) =>
  typeof v === 'string' ? `string(${v.length})` : Array.isArray(v) ? `array(${v.length})` : typeof v;

// ---------------------------------------------------------------- clip + media
let clipId = process.argv.slice(2).find((a) => !a.startsWith('--'));
let media;
if (!clipId) {
  const { DatabaseSync } = await import('node:sqlite');
  const con = new DatabaseSync(dbPath, { readOnly: true });
  const row = con
    .prepare("select id, media_urls from clips where media_urls is not null and media_urls != '[]' limit 1")
    .get();
  clipId = row.id;
  media = JSON.parse(row.media_urls)[0];
} else {
  // An explicit id still needs its media entry; ask the feed for it.
  const res = await fetch('https://studio-api-prod.suno.com/api/feed/v3', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ page: 1 }),
  });
  console.log('feed status:', res.status, '(a 401/403 here means this probe needs a bearer)');
  const body = await res.json();
  const clips = body?.data?.clips ?? body?.clips ?? [];
  const hit = clips.find((c) => c.id === clipId);
  media = hit?.media_urls?.[0];
}
if (!media) {
  console.log('no media_urls[] entry found for that clip');
  process.exit(1);
}
console.log('clip id (hashed) :', sha(clipId));
console.log('media entry      :', {
  content_type: media.content_type,
  delivery: media.delivery,
  encoding: media.encoding,
  url_shape: shape(media.url),
});

// ------------------------------------------------------------------- the license
const authHeaders = bearer ? { 'Authorization': `Bearer ${bearer}` } : {};
const res = await fetch(RIGHTS_URL, {
  method: 'POST',
  headers: { 'Content-Type': 'application/json', ...authHeaders },
  body: JSON.stringify({ content_params: { content_id: clipId, content_type: 'clip' } }),
});
console.log('\nrights status    :', res.status, res.statusText);
const text = await res.text();
if (!res.ok) {
  console.log('rights body      :', text.slice(0, 300));
  process.exit(1);
}
const payload = JSON.parse(text);
const data = payload?.data ?? payload;
console.log('rights fields    :', Object.entries(data).map(([k, v]) => `${k}:${describe(v)}`).join(' '));
console.log('rights envelope  :', Object.keys(payload).join(' '));

// ----------------------------------------------------------------- the unwrap
// The server wraps with SHA-256 of whatever token the request carried, and
// hands back a guest token when the request carried none. Try the bearer
// first, then the guest token -- exactly the fallback the site's own client
// performs.
const wrapperFor = async (token) =>
  subtle.importKey('raw', await subtle.digest('SHA-256', enc.encode(token)), 'AES-GCM', false, ['decrypt']);

const unwrapWith = async (label, token) => {
  if (!token) return null;
  const wrapper = await wrapperFor(token);
  const out = {};
  for (const field of ['key', 'iv']) {
    const bytes = Uint8Array.from(Buffer.from(data[field], 'base64'));
    console.log(`   ${field} blob     :`, bytes.length, 'bytes (12 nonce + payload + 16 tag)');
    try {
      out[field] = new Uint8Array(await subtle.decrypt(
        { name: 'AES-GCM', iv: bytes.slice(0, 12), additionalData: enc.encode(clipId) },
        wrapper, bytes.slice(12)));
    } catch (e) {
      console.log(`   ${label} unwrap failed (${e.name})`);
      return null;
    }
  }
  console.log(`unwrapped with ${label}: key ${out.key.byteLength} bytes, counter ${out.iv.byteLength} bytes`);
  return out;
};

let plain = (bearer ? await unwrapWith('SHA-256(bearer)', bearer) : null)
  ?? await unwrapWith('SHA-256(glt)', data.glt);
if (!plain) process.exit(2);
const rawKey = plain.key;
const rawCounter = plain.iv;

// --------------------------------------------------------- decrypt the head
const key = await subtle.importKey('raw', rawKey, 'AES-CTR', false, ['decrypt']);
const head = new Uint8Array(await (await fetch(media.url, { headers: { Range: 'bytes=0-4095' } })).arrayBuffer());
const clear = new Uint8Array(await subtle.decrypt(
  { name: 'AES-CTR', counter: rawCounter, length: 128 }, key, head));
const box = Buffer.from(clear.slice(4, 8)).toString('latin1');
const ebml = clear[0] === 0x1a && clear[1] === 0x45 && clear[2] === 0xdf && clear[3] === 0xa3;
console.log('\nencrypted head   :', Buffer.from(head.slice(0, 16)).toString('hex'));
console.log('decrypted head   :', Buffer.from(clear.slice(0, 16)).toString('hex'));
console.log('decrypted magic  :', box === 'ftyp' ? 'MP4 (audio/mp4)' : ebml ? 'WebM' : 'UNKNOWN');
console.log('\nA capture is complete when the rights status is 200 and the decrypted magic is a container.');
if (process.env.MANGO_SAVE) {
  const out = path.resolve(process.env.MANGO_SAVE);
  fs.writeFileSync(out, clear);
  console.log('wrote head to', out);
}
