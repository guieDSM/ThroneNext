import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

let onMessage;
let onEvent;
let onDetach;
let sent;
let failFetch = false;
let fetchCount = 0;
const commands = [];
const session = new Map();
const chrome = {
  runtime: {onMessage: {addListener(fn) { onMessage = fn; }}},
  storage: {session: {
    async get(key) { return {[key]: session.get(key)}; },
    async set(values) { for (const [key, value] of Object.entries(values)) session.set(key, value); },
    async remove(key) { session.delete(key); }
  }},
  tabs: {
    async query() { return [{id: 7, url: 'https://example.com/episodes/example/'}]; },
    async reload() {}
  },
  action: {
    async setBadgeText() {},
    async setBadgeBackgroundColor() {}
  },
  debugger: {
    onEvent: {addListener(fn) { onEvent = fn; }},
    onDetach: {addListener(fn) { onDetach = fn; }},
    async attach() {},
    async detach(source) { onDetach(source); },
    async sendCommand(source, method) {
      commands.push({source, method});
      if (method === 'Network.getResponseBody') {
        return {body: source.sessionId
          ? '<html><title>404 Not Found</title><h1>404 Not Found</h1></html>'
          : '<html><title>Episode</title></html>', base64Encoded: false};
      }
      return {};
    }
  }
};
const context = {chrome, URL, atob, setTimeout, clearTimeout,
  fetch: async (_url, request) => {
    fetchCount++;
    if (failFetch) throw new Error('bridge unavailable');
    sent = JSON.parse(request.body);
    return {ok: true};
  }};
vm.runInNewContext(fs.readFileSync(new URL('./background.js', import.meta.url), 'utf8'), {...context});

function message(command) {
  return new Promise(resolve => onMessage({command}, {}, resolve));
}

assert.equal((await message('start')).active, true);
onEvent({tabId: 7}, 'Target.attachedToTarget', {sessionId: 'iframe-1'});
await new Promise(resolve => setImmediate(resolve));
assert(commands.some(x => x.source.sessionId === 'iframe-1' && x.method === 'Network.enable'));

const unrelated = {tabId: 8};
onEvent(unrelated, 'Network.requestWillBeSent', {requestId: 'wrong', request: {url: 'https://example.edu/'}});
onEvent(unrelated, 'Network.responseReceived', {requestId: 'wrong', type: 'Document',
  response: {url: 'https://example.edu/', status: 404, mimeType: 'text/html'}});

const frame = {tabId: 7, sessionId: 'iframe-1'};
onEvent(frame, 'Network.requestWillBeSent', {requestId: 'one', request: {url: 'https://example.org/player?id=private'}});
onEvent(frame, 'Network.responseReceived', {requestId: 'one', type: 'Document',
  response: {url: 'https://example.org/player?id=private', status: 200, mimeType: 'text/html'}});
onEvent(frame, 'Network.loadingFinished', {requestId: 'one', encodedDataLength: 545});

onEvent({tabId: 7}, 'Network.requestWillBeSent', {requestId: 'ad', request: {url: 'https://ads.example.net/ad.js'}});
onEvent({tabId: 7}, 'Network.loadingFailed', {requestId: 'ad', errorText: 'net::ERR_BLOCKED_BY_CLIENT'});
await new Promise(resolve => setImmediate(resolve));

const live = await message('status');
assert.equal(live.active, true);
assert(live.snapshot.hosts.some(item => item.host === 'example.org'));
assert(live.snapshot.hosts.some(item => item.host === 'ads.example.net'
  && item.issues.includes('browser_blocked')));
assert(!live.snapshot.hosts.some(item => item.host === 'example.edu'));
assert.deepEqual(JSON.parse(JSON.stringify(live.snapshot.events)),
  [{host: 'example.org', reason: 'html_error'}]);

assert.equal((await message('stop')).active, false);
assert.equal(sent.site, 'example.com');
assert.deepEqual(JSON.parse(JSON.stringify(sent.events)), [{host: 'example.org', reason: 'html_error'}]);
assert.equal(sent.blocked, 1);
assert(!('hosts' in sent));
assert(!JSON.stringify(sent).includes('private'));
assert.equal((await message('status')).snapshot.delivered, true);
vm.runInNewContext(fs.readFileSync(new URL('./background.js', import.meta.url), 'utf8'), context);
assert.equal((await message('status')).snapshot.delivered, true);
assert((await message('status')).snapshot.hosts.some(item => item.host === 'example.org'));
failFetch = true;
assert.match((await message('send')).message, /не принял/);
assert.equal((await message('status')).snapshot.delivered, false);
failFetch = false;
assert.equal((await message('send')).snapshot.delivered, true);
assert.equal(fetchCount, 3);
console.log('capture test passed');
