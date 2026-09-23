const BRIDGE = 'http://127.0.0.1:18472/capture';
const ERROR_PAGE = /<(?:title|h1)(?:\s+[^>]*)?>\s*(?:403 Forbidden|404 Not Found|502 Bad Gateway|503 Service Unavailable)\s*<\/(?:title|h1)>/i;
const MAX_HOSTS = 500;
const MAX_EVENTS = 100;
let capture = null;
let lastCapture = null;

function hostOf(url) {
  try {
    const parsed = new URL(url);
    return /^https?:$/.test(parsed.protocol) ? parsed.hostname.toLowerCase() : '';
  } catch {
    return '';
  }
}

function addEvent(state, url, reason) {
  const host = hostOf(url);
  if (!host || !['html_error', 'http_error', 'network_error'].includes(reason)) return;
  const key = `${host}|${reason}`;
  if (state.seen.has(key)) return;
  state.seen.add(key);
  state.events.push({host, reason});
  markIssue(state, host, reason);
}

function observeRequest(state, url, type) {
  const host = hostOf(url);
  if (!host) return;
  let item = state.hosts.get(host);
  if (!item) {
    if (state.hosts.size >= MAX_HOSTS) {
      state.hostOverflow++;
      return;
    }
    item = {host, requests: 0, types: new Set(), issues: new Set()};
    state.hosts.set(host, item);
  }
  item.requests++;
  if (type) item.types.add(type);
}

function markIssue(state, host, reason) {
  state.hosts.get(host)?.issues.add(reason);
}

function issuePriority(item) {
  return item.issues.some(issue => issue !== 'browser_blocked') ? 2
    : item.issues.length ? 1 : 0;
}

function snapshotOf(state) {
  const hosts = [...state.hosts.values()].map(item => ({
    host: item.host,
    requests: item.requests,
    types: [...item.types].slice(0, 5),
    issues: [...item.issues]
  }));
  hosts.sort((a, b) => issuePriority(b) - issuePriority(a)
    || b.requests - a.requests || a.host.localeCompare(b.host));
  return {
    site: state.site,
    hosts,
    hostOverflow: state.hostOverflow,
    events: state.events.slice(0, MAX_EVENTS),
    eventOverflow: Math.max(0, state.events.length - MAX_EVENTS),
    blocked: state.blocked,
    delivered: false,
    interrupted: false
  };
}

async function savedCapture() {
  if (lastCapture) return lastCapture;
  lastCapture = (await chrome.storage.session.get('lastCapture')).lastCapture || null;
  return lastCapture;
}

async function saveCapture(snapshot) {
  lastCapture = snapshot;
  await chrome.storage.session.set({lastCapture: snapshot});
}

async function captureStatus() {
  return {active: Boolean(capture), snapshot: capture ? snapshotOf(capture) : await savedCapture(), message: ''};
}

async function enableSession(source) {
  await chrome.debugger.sendCommand(source, 'Network.enable');
  await chrome.debugger.sendCommand(source, 'Target.setAutoAttach', {
    autoAttach: true,
    waitForDebuggerOnStart: false,
    flatten: true,
    filter: [{type: 'iframe', exclude: false}]
  });
}

async function startCapture() {
  if (capture) return {active: true, message: 'Захват уже запущен.'};
  const [tab] = await chrome.tabs.query({active: true, currentWindow: true});
  const site = hostOf(tab?.url);
  if (!tab?.id || !site) return {active: false, message: 'Откройте обычную страницу http(s).'};
  const state = {
    tabId: tab.id,
    site,
    requests: new Map(),
    events: [],
    seen: new Set(),
    hosts: new Map(),
    hostOverflow: 0,
    blocked: 0,
    pending: new Set(),
    timer: null
  };
  try {
    await chrome.debugger.attach({tabId: tab.id}, '1.3');
    capture = state;
    lastCapture = null;
    await chrome.storage.session.remove('lastCapture');
    await enableSession({tabId: tab.id});
    await chrome.action.setBadgeText({tabId: tab.id, text: 'REC'});
    await chrome.action.setBadgeBackgroundColor({tabId: tab.id, color: '#b73535'});
    state.timer = setTimeout(() => { stopCapture().catch(() => {}); }, 180000);
    await chrome.tabs.reload(tab.id, {bypassCache: true});
    return {active: true, message: 'Захват идёт. Запустите плеер, затем завершите проверку.'};
  } catch (error) {
    capture = null;
    try { await chrome.debugger.detach({tabId: tab.id}); } catch {}
    return {active: false, message: `Не удалось подключиться к вкладке: ${error.message}. Закройте DevTools и повторите.`};
  }
}

async function stopCapture() {
  const state = capture;
  if (!state) return {active: false, message: 'Захват не запущен.'};
  clearTimeout(state.timer);
  await Promise.allSettled([...state.pending]);
  capture = null;
  try { await chrome.debugger.detach({tabId: state.tabId}); } catch {}
  try { await chrome.action.setBadgeText({tabId: state.tabId, text: ''}); } catch {}
  const snapshot = snapshotOf(state);
  await saveCapture(snapshot);
  return sendCapture();
}

async function sendCapture() {
  const snapshot = await savedCapture();
  if (!snapshot) return {active: false, message: 'Сначала выполните захват вкладки.'};
  const payload = JSON.stringify({site: snapshot.site, events: snapshot.events, blocked: snapshot.blocked});
  try {
    const response = await fetch(BRIDGE, {
      method: 'POST',
      headers: {'Content-Type': 'text/plain', 'X-Throne-Capture': '1'},
      body: payload
    });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    snapshot.delivered = true;
    await saveCapture(snapshot);
    return {active: false, snapshot, message: `Передано в Throne: ${snapshot.events.length} событий, блокировок браузером: ${snapshot.blocked}. Проверьте список кандидатов.`};
  } catch (error) {
    snapshot.delivered = false;
    await saveCapture(snapshot);
    return {active: false, snapshot, message: `Throne не принял захват (${error.message}). Откройте окно «Добавить исключение» и нажмите «Повторить передачу».`};
  }
}

chrome.runtime.onMessage.addListener((message, _sender, sendResponse) => {
  const operation = message.command === 'status' ? captureStatus
    : message.command === 'start' ? startCapture
    : message.command === 'stop' ? stopCapture
    : message.command === 'send' ? sendCapture : null;
  if (!operation) {
    sendResponse({active: Boolean(capture), message: 'Неизвестная команда.'});
    return;
  }
  operation().then(sendResponse, error => sendResponse({active: Boolean(capture), message: error.message}));
  return true;
});

chrome.debugger.onEvent.addListener((source, method, params) => {
  const state = capture;
  if (!state || source.tabId !== state.tabId) return;
  if (method === 'Target.attachedToTarget') {
    enableSession({tabId: state.tabId, sessionId: params.sessionId}).catch(() => {});
    return;
  }
  const key = `${source.sessionId || 'root'}:${params.requestId}`;
  if (method === 'Network.requestWillBeSent') {
    const url = params.request?.url || '';
    const type = params.type || '';
    observeRequest(state, url, type);
    state.requests.set(key, {url, type});
  } else if (method === 'Network.responseReceived') {
    const previous = state.requests.get(key) || {};
    const response = params.response || {};
    const item = {
      url: response.url || previous.url,
      type: params.type || previous.type,
      status: response.status || 0,
      mimeType: response.mimeType || '',
      contentLength: Number(response.headers?.['content-length'] || response.headers?.['Content-Length'] || 0)
    };
    if (!previous.url) observeRequest(state, item.url, item.type);
    else if (item.type) state.hosts.get(hostOf(item.url))?.types.add(item.type);
    state.requests.set(key, item);
    if (item.status >= 400) addEvent(state, item.url, 'http_error');
  } else if (method === 'Network.loadingFailed') {
    const item = state.requests.get(key);
    if (params.errorText?.includes('ERR_BLOCKED_BY_CLIENT')) {
      state.blocked++;
      if (item) markIssue(state, hostOf(item.url), 'browser_blocked');
    }
    else if (item) addEvent(state, item.url, 'network_error');
    state.requests.delete(key);
  } else if (method === 'Network.loadingFinished') {
    const item = state.requests.get(key);
    state.requests.delete(key);
    if (!item || item.type !== 'Document' || !item.mimeType.startsWith('text/html') ||
        item.status >= 400 || item.contentLength > 16384 || params.encodedDataLength > 16384) return;
    const pending = chrome.debugger.sendCommand(source, 'Network.getResponseBody', {requestId: params.requestId})
      .then(response => {
        let body = response?.body || '';
        if (response?.base64Encoded) body = atob(body);
        if (body.length <= 8192 && ERROR_PAGE.test(body)) addEvent(state, item.url, 'html_error');
      })
      .catch(() => {});
    state.pending.add(pending);
    pending.finally(() => state.pending.delete(pending));
  }
});

chrome.debugger.onDetach.addListener(source => {
  if (capture?.tabId === source.tabId) {
    const state = capture;
    clearTimeout(state.timer);
    capture = null;
    const snapshot = snapshotOf(state);
    snapshot.interrupted = true;
    saveCapture(snapshot).catch(() => {});
    chrome.action.setBadgeText({tabId: source.tabId, text: ''}).catch(() => {});
  }
});
