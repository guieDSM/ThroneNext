const elements = {
  start: document.getElementById('start'),
  stop: document.getElementById('stop'),
  send: document.getElementById('send'),
  status: document.getElementById('status'),
  filter: document.getElementById('filter'),
  hosts: document.getElementById('hosts'),
  hostCount: document.getElementById('host-count'),
  hostOverflow: document.getElementById('host-overflow'),
  events: document.getElementById('events'),
  payloadMeta: document.getElementById('payload-meta'),
  delivery: document.getElementById('delivery')
};

const issueNames = {
  html_error: 'Страница ошибки',
  http_error: 'HTTP-ошибка',
  network_error: 'Ошибка соединения',
  browser_blocked: 'Блокировка браузером'
};

const typeNames = {
  Document: 'страница',
  Script: 'скрипт',
  Media: 'видео/аудио',
  XHR: 'запрос',
  Fetch: 'запрос',
  Image: 'изображение',
  Stylesheet: 'стиль',
  Font: 'шрифт'
};

let current = null;
let busy = false;

function empty(container, message) {
  container.replaceChildren();
  const item = document.createElement('div');
  item.className = 'empty';
  item.textContent = message;
  container.append(item);
}

function row(host, detail, meta) {
  const item = document.createElement('div');
  item.className = 'row';
  const top = document.createElement('div');
  top.className = 'row-top';
  const name = document.createElement('span');
  name.className = 'host';
  name.textContent = host;
  const right = document.createElement('span');
  right.className = 'count';
  right.textContent = detail;
  top.append(name, right);
  item.append(top);
  if (meta) {
    const caption = document.createElement('div');
    caption.className = 'meta';
    caption.textContent = meta;
    item.append(caption);
  }
  return item;
}

function renderHosts(snapshot) {
  const hosts = snapshot?.hosts || [];
  elements.hostCount.textContent = `${hosts.length}${snapshot?.hostOverflow ? '+' : ''} доменов`;
  elements.hostOverflow.hidden = !snapshot?.hostOverflow;
  elements.hostOverflow.textContent = snapshot?.hostOverflow
    ? `Ещё ${snapshot.hostOverflow} обращения сверх лимита отображения.` : '';
  if (!hosts.length) {
    empty(elements.hosts, snapshot ? 'Запросов пока нет. Обновите страницу и запустите плеер.'
      : 'Запустите захват, чтобы увидеть домены этой вкладки.');
    return;
  }
  const query = elements.filter.value.trim().toLowerCase();
  const filtered = hosts.filter(item => item.host.includes(query));
  elements.hosts.replaceChildren();
  if (!filtered.length) {
    empty(elements.hosts, 'По этому поиску доменов нет.');
    return;
  }
  const fragment = document.createDocumentFragment();
  for (const item of filtered) {
    const kinds = [...new Set((item.types || []).map(type => typeNames[type] || 'другое'))];
    const problems = (item.issues || []).map(issue => issueNames[issue] || issue);
    const detail = `${item.requests} запр.`;
    const meta = [item.host === snapshot.site ? 'основной сайт' : '', ...kinds, ...problems].filter(Boolean).join(' · ');
    fragment.append(row(item.host, detail, meta));
  }
  elements.hosts.append(fragment);
}

function renderPayload(snapshot, active) {
  elements.send.hidden = active || !snapshot || snapshot.delivered;
  if (!snapshot) {
    elements.delivery.className = 'count';
    elements.delivery.textContent = 'Нет захвата';
    elements.payloadMeta.textContent = 'Домен сайта, домены ошибок и число блокировок браузером.';
    empty(elements.events, 'Пока нечего передавать.');
    return;
  }
  elements.delivery.className = `count ${snapshot.delivered ? 'sent' : 'unsent'}`;
  elements.delivery.textContent = active ? 'Сбор данных' : snapshot.delivered ? 'Передано' : 'Не передано';
  elements.payloadMeta.textContent = `Сайт: ${snapshot.site} · ошибок: ${snapshot.events.length}${snapshot.eventOverflow ? '+' : ''} · блокировок браузером: ${snapshot.blocked}`;
  elements.events.replaceChildren();
  if (!snapshot.events.length) {
    empty(elements.events, 'Домены с сетевыми ошибками не найдены. Throne получит адрес сайта и число блокировок.');
    return;
  }
  const fragment = document.createDocumentFragment();
  for (const event of snapshot.events) {
    fragment.append(row(event.host, '', issueNames[event.reason] || event.reason));
  }
  elements.events.append(fragment);
  if (snapshot.eventOverflow) {
    const note = document.createElement('div');
    note.className = 'overflow';
    note.textContent = `Ещё ${snapshot.eventOverflow} событий не войдут в передачу (лимит 100).`;
    elements.events.append(note);
  }
}

function render(state) {
  current = state;
  elements.start.disabled = busy || state.active;
  elements.stop.disabled = busy || !state.active;
  elements.send.disabled = busy || state.active;
  renderHosts(state.snapshot);
  renderPayload(state.snapshot, state.active);
  if (state.active && !busy) {
    elements.status.className = '';
    elements.status.textContent = 'Захват идёт. Откройте сайт или плеер, затем завершите проверку.';
  } else if (!state.active && state.snapshot?.interrupted && !state.snapshot.delivered) {
    elements.status.className = 'error';
    elements.status.textContent = 'Захват прерван. Собранные домены сохранены; можно повторить передачу.';
  }
}

async function refresh() {
  try {
    render(await chrome.runtime.sendMessage({command: 'status'}));
  } catch (error) {
    elements.status.className = 'error';
    elements.status.textContent = `Не удалось получить состояние: ${error.message}`;
  }
}

async function call(command) {
  if (busy) return;
  busy = true;
  if (current) render(current);
  let resultMessage = '';
  let resultError = false;
  try {
    const result = await chrome.runtime.sendMessage({command});
    await refresh();
    resultMessage = result.message || '';
    resultError = resultMessage.includes('не принял') || resultMessage.includes('Не удалось');
  } catch (error) {
    resultMessage = String(error.message || error);
    resultError = true;
  } finally {
    busy = false;
    if (current) render(current);
    if (resultMessage) {
      elements.status.className = resultError ? 'error' : '';
      elements.status.textContent = resultMessage;
    }
  }
}

elements.start.addEventListener('click', () => call('start'));
elements.stop.addEventListener('click', () => call('stop'));
elements.send.addEventListener('click', () => call('send'));
elements.filter.addEventListener('input', () => {
  if (current) renderHosts(current.snapshot);
});
refresh();
setInterval(() => { if (current?.active && !busy) refresh(); }, 1000);
