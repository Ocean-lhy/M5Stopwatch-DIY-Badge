(function () {
  const $ = s => document.querySelector(s);
  const $$ = s => Array.from(document.querySelectorAll(s));
  const C = window.Compose;
  const TEXT_FIELDS = ['name', 'title', 'company', 'tags', 'intro', 'seeking', 'booth', 'email', 'phone'];
  const MAX_SLOTS = 3;

  const state = {
    view: 'card',
    theme: 0,
    brightness: 200,
    ambientBrightness: 40,
    ambientAfter: 45,
    scanTimeout: 30,
    avatar: { image: null, zoom: 1, ox: 0, oy: 0 },
    avatarDirty: false,
    battery: -1,
    runtime: -1,
    dirty: false,
    busy: false,
    scanIndex: 0,
  };

  // ------------------------------------------------------------ utilities
  function toast(message) {
    const el = $('#toast');
    el.textContent = message;
    el.classList.add('show');
    clearTimeout(el._t);
    el._t = setTimeout(() => el.classList.remove('show'), 2600);
  }

  async function api(path, opt = {}, timeout = 10000) {
    const ctl = new AbortController();
    const timer = setTimeout(() => ctl.abort(), timeout);
    try {
      const r = await fetch(path, { ...opt, signal: ctl.signal, cache: 'no-store' });
      const body = await r.json().catch(() => ({}));
      if (!r.ok) throw new Error(body.message || `HTTP ${r.status}`);
      return body;
    } finally {
      clearTimeout(timer);
    }
  }

  function loadImage(src) {
    return new Promise((resolve, reject) => {
      const img = new Image();
      img.onload = () => resolve(img);
      img.onerror = () => reject(new Error('image load failed'));
      img.src = src;
    });
  }

  function canvasBlob(canvas, quality) {
    return new Promise(resolve => canvas.toBlob(resolve, 'image/jpeg', quality));
  }

  function setConn(kind, text) {
    $('#conn').className = 'conn ' + kind;
    $('#connText').textContent = text;
  }

  function markDirty() {
    state.dirty = true;
    const s = $('#pubStatus');
    s.className = 'pub-status dirty';
    s.textContent = '未发布';
    schedulePreview();
  }

  // ------------------------------------------------------------ model
  function model() {
    const m = {};
    for (const id of TEXT_FIELDS) m[id] = $('#' + id).value.trim();
    m.about = $('#about').checked;
    m.touch = $('#touch').checked;
    m.qr = slots().map(s => ({ label: s.querySelector('.s-label').value.trim(), text: s.querySelector('.s-text').value.trim() }))
      .filter(q => q.text);
    return m;
  }

  function theme() {
    return C.THEMES[state.theme] || C.THEMES[0];
  }

  // ------------------------------------------------------------ preview
  const preview = $('#preview').getContext('2d');
  const strip = document.createElement('canvas');
  strip.width = 360;
  strip.height = 96;
  let previewQueued = false;

  function schedulePreview() {
    if (previewQueued) return;
    previewQueued = true;
    requestAnimationFrame(() => {
      previewQueued = false;
      drawPreview();
    });
  }

  function drawPreview() {
    const m = model();
    const t = theme();
    preview.save();
    if (state.view === 'card') {
      C.drawCard(preview, m, state.avatar, t);
      C.drawStatusPreview(preview, t, state.battery, state.runtime);
    } else if (state.view === 'about') {
      C.drawAbout(preview, m, t);
      C.drawStatusPreview(preview, t, state.battery, state.runtime);
    } else if (state.view === 'scan') {
      C.drawScanPreview(preview, m, t, state.scanIndex);
    } else {
      C.drawNameStrip(strip.getContext('2d'), m);
      C.drawAmbientPreview(preview, m, strip, state.battery, state.runtime);
    }
    preview.restore();
  }

  $$('#previewTabs button').forEach(b => b.addEventListener('click', () => {
    if (state.view === 'scan' && b.dataset.view === 'scan') state.scanIndex++;
    state.view = b.dataset.view;
    $$('#previewTabs button').forEach(x => x.setAttribute('aria-selected', String(x === b)));
    drawPreview();
  }));

  // ------------------------------------------------------------ avatar cropper
  const crop = $('#crop');
  const cropCtx = crop.getContext('2d');

  function clampAvatar() {
    const a = state.avatar;
    if (!a.image) return;
    const iw = a.image.naturalWidth, ih = a.image.naturalHeight;
    const side = Math.min(iw, ih) / a.zoom;
    const mx = (iw - side) / (2 * side), my = (ih - side) / (2 * side);
    a.ox = Math.max(-mx, Math.min(mx, a.ox));
    a.oy = Math.max(-my, Math.min(my, a.oy));
  }

  function drawCrop() {
    const s = crop.width;
    cropCtx.fillStyle = '#e9eef2';
    cropCtx.fillRect(0, 0, s, s);
    const a = state.avatar;
    if (a.image) {
      const iw = a.image.naturalWidth, ih = a.image.naturalHeight;
      const side = Math.min(iw, ih) / a.zoom;
      cropCtx.drawImage(a.image, (iw - side) / 2 - a.ox * side, (ih - side) / 2 - a.oy * side, side, side, 0, 0, s, s);
    }
    cropCtx.save();
    cropCtx.fillStyle = 'rgba(20,30,40,.45)';
    cropCtx.beginPath();
    cropCtx.rect(0, 0, s, s);
    cropCtx.arc(s / 2, s / 2, s / 2 - 6, 0, Math.PI * 2, true);
    cropCtx.fill('evenodd');
    cropCtx.strokeStyle = 'rgba(255,255,255,.9)';
    cropCtx.lineWidth = 3;
    cropCtx.beginPath();
    cropCtx.arc(s / 2, s / 2, s / 2 - 6, 0, Math.PI * 2);
    cropCtx.stroke();
    cropCtx.restore();
  }

  function avatarChanged() {
    state.avatarDirty = true;
    clampAvatar();
    drawCrop();
    markDirty();
  }

  let drag = null;
  crop.addEventListener('pointerdown', e => {
    if (!state.avatar.image) return;
    crop.setPointerCapture(e.pointerId);
    drag = { x: e.clientX, y: e.clientY, ox: state.avatar.ox, oy: state.avatar.oy };
  });
  crop.addEventListener('pointermove', e => {
    if (!drag) return;
    const size = crop.getBoundingClientRect().width;
    state.avatar.ox = drag.ox + (e.clientX - drag.x) / size;
    state.avatar.oy = drag.oy + (e.clientY - drag.y) / size;
    avatarChanged();
  });
  const endDrag = () => { drag = null; };
  crop.addEventListener('pointerup', endDrag);
  crop.addEventListener('pointercancel', endDrag);
  $('#zoom').addEventListener('input', e => {
    state.avatar.zoom = Number(e.target.value);
    avatarChanged();
  });
  $('#avatarFile').addEventListener('change', async e => {
    const file = e.target.files[0];
    if (!file) return;
    try {
      const url = URL.createObjectURL(file);
      state.avatar = { image: await loadImage(url), zoom: 1, ox: 0, oy: 0 };
      $('#zoom').value = 1;
      avatarChanged();
    } catch (err) {
      toast('无法读取这张图片，请换一张 JPG/PNG');
    }
  });

  // ------------------------------------------------------------ QR slots
  function slots() {
    return $$('#slots .slot');
  }

  function renumberSlots() {
    slots().forEach((s, i) => { s.querySelector('.slot-title').textContent = `二维码 ${i + 1}`; });
    const full = slots().length >= MAX_SLOTS;
    $('#addSlot').disabled = full;
    $('#addVcard').disabled = full;
  }

  function addSlot(label = '', text = '') {
    if (slots().length >= MAX_SLOTS) return null;
    const node = $('#slotTpl').content.firstElementChild.cloneNode(true);
    node.querySelector('.s-label').value = label;
    node.querySelector('.s-text').value = text;
    node.querySelectorAll('input:not([type=file])').forEach(i => i.addEventListener('input', markDirty));
    node.querySelector('.remove').addEventListener('click', () => {
      node.remove();
      renumberSlots();
      markDirty();
    });
    node.querySelector('.s-file').addEventListener('change', e => recognize(node, e.target));
    $('#slots').appendChild(node);
    renumberSlots();
    return node;
  }

  async function waitFor(check, ms = 5000) {
    const end = Date.now() + ms;
    while (!check() && Date.now() < end) await new Promise(r => setTimeout(r, 50));
    return check();
  }

  // WeChat codes use rounded finders and a logo, so jsQR needs a few crops/scales.
  async function decodeQr(img) {
    if ('BarcodeDetector' in window) {
      try {
        const found = await new window.BarcodeDetector({ formats: ['qr_code'] }).detect(img);
        if (found[0] && found[0].rawValue) return found[0].rawValue;
      } catch (e) { /* fall back to jsQR */ }
    }
    if (!(await waitFor(() => window.jsQR))) return null;
    const iw = img.naturalWidth, ih = img.naturalHeight;
    const c = document.createElement('canvas');
    const ctx = c.getContext('2d', { willReadFrequently: true });
    // The code sits slightly below the middle of a WeChat "My QR code" screenshot.
    for (const fraction of [1, 0.85, 0.75]) {
      for (const center of [0.5, 0.55]) {
      for (const target of [480, 400, 640]) {
        const side = Math.min(iw, ih) * fraction;
        const sy = Math.max(0, Math.min(ih - side, ih * center - side / 2));
        c.width = c.height = target;
        ctx.fillStyle = '#fff';
        ctx.fillRect(0, 0, target, target);
        ctx.drawImage(img, (iw - side) / 2, sy, side, side, 0, 0, target, target);
        const data = ctx.getImageData(0, 0, target, target);
        const r = window.jsQR(data.data, target, target, { inversionAttempts: 'attemptBoth' });
        if (r && r.data) return r.data;
        await new Promise(res => setTimeout(res));
      }
      }
    }
    return null;
  }

  async function recognize(node, input) {
    const file = input.files[0];
    if (!file) return;
    const status = node.querySelector('.s-status');
    status.className = 's-status busy';
    status.textContent = '识别中…';
    try {
      const img = await loadImage(URL.createObjectURL(file));
      const text = await decodeQr(img);
      if (!text) throw new Error('not found');
      if (text.length > 250) throw new Error('too long');
      node.querySelector('.s-text').value = text;
      const label = node.querySelector('.s-label');
      if (!label.value && /wechat\.com|weixin/i.test(text)) label.value = '微信扫一扫';
      status.className = 's-status ok';
      status.textContent = '已识别';
      state.view = 'scan';
      state.scanIndex = slots().indexOf(node);
      $$('#previewTabs button').forEach(x => x.setAttribute('aria-selected', String(x.dataset.view === 'scan')));
      markDirty();
    } catch (e) {
      status.className = 's-status err';
      status.textContent = e.message === 'too long' ? '内容太长，胸牌放不下' : '未识别到二维码';
    } finally {
      input.value = '';
    }
  }

  function vcard() {
    const m = model();
    const esc = s => s.replace(/([\\;,:])/g, '\\$1');
    const parts = [`N:${esc(m.name || '')}`];
    if (m.phone) parts.push(`TEL:${esc(m.phone)}`);
    if (m.email) parts.push(`EMAIL:${esc(m.email)}`);
    if (m.company) parts.push(`ORG:${esc(m.company)}`);
    if (m.title) parts.push(`NOTE:${esc(m.title)}`);
    return `MECARD:${parts.join(';')};;`;
  }

  $('#addSlot').addEventListener('click', () => { addSlot(); markDirty(); });
  $('#addVcard').addEventListener('click', () => {
    if (!$('#phone').value.trim() && !$('#email').value.trim()) {
      toast('先填写邮箱或电话');
      $('#email').focus();
      return;
    }
    addSlot('扫码保存联系方式', vcard());
    markDirty();
  });

  // ------------------------------------------------------------ appearance
  function renderThemes() {
    const box = $('#themes');
    box.innerHTML = '';
    C.THEMES.forEach(t => {
      const b = document.createElement('button');
      b.type = 'button';
      b.className = 'theme';
      b.setAttribute('role', 'radio');
      b.setAttribute('aria-checked', String(t.id === state.theme));
      b.innerHTML = `<i style="background:${t.bg};--dot:${t.accent}"></i><span>${t.name}</span>`;
      b.addEventListener('click', () => {
        state.theme = t.id;
        renderThemes();
        markDirty();
      });
      box.appendChild(b);
    });
  }

  function bindSeg(id, key) {
    const box = $('#' + id);
    const sync = () => box.querySelectorAll('button').forEach(b => b.classList.toggle('on', Number(b.dataset.v) === state[key]));
    box.querySelectorAll('button').forEach(b => b.addEventListener('click', () => {
      state[key] = Number(b.dataset.v);
      sync();
      markDirty();
    }));
    return sync;
  }
  const syncAmbient = bindSeg('ambientAfter', 'ambientAfter');
  const syncScan = bindSeg('scanTimeout', 'scanTimeout');

  $('#brightness').addEventListener('input', e => {
    state.brightness = Number(e.target.value);
    $('#brightnessOut').textContent = e.target.value;
    markDirty();
  });
  TEXT_FIELDS.forEach(id => $('#' + id).addEventListener('input', markDirty));
  ['about', 'touch'].forEach(id => $('#' + id).addEventListener('change', markDirty));

  // ------------------------------------------------------------ device state
  function applyDevice(d) {
    if (!d) return;
    state.battery = typeof d.battery === 'number' ? d.battery : -1;
    state.runtime = !d.charging && typeof d.runtime === 'number' ? d.runtime : -1;
    const runtime = state.runtime >= 0 ? ` · ${state.runtime.toFixed(1)}h` : '';
    $('#devBattery').textContent = state.battery >= 0 ? `${state.battery}%${d.charging ? ' 充电中' : runtime}` : '--';
    $('#devScans').textContent = d.scans != null ? `${d.scans} 次` : '--';
    $('#devSsid').textContent = d.ssid || '--';
  }

  // Fills the form once; later polls only refresh telemetry so typing is never overwritten.
  function applyConfig(s) {
    for (const id of TEXT_FIELDS) $('#' + id).value = s[id] || '';
    $('#about').checked = s.about !== false;
    $('#touch').checked = s.touch !== false;
    state.theme = s.theme || 0;
    state.brightness = s.brightness || 200;
    state.ambientBrightness = s.ambientBrightness ?? 40;
    state.ambientAfter = s.ambientAfter ?? 45;
    state.scanTimeout = s.scanTimeout || 30;
    $('#brightness').value = state.brightness;
    $('#brightnessOut').textContent = state.brightness;
    $('#slots').innerHTML = '';
    (s.qr || []).forEach(q => addSlot(q.label, q.text));
    if (!slots().length) addSlot();
    renderThemes();
    syncAmbient();
    syncScan();
  }

  async function syncTime() {
    try {
      await api('/api/time', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ time: Math.floor(Date.now() / 1000), tz: -new Date().getTimezoneOffset() }),
      });
    } catch (e) { /* not fatal */ }
  }

  async function boot() {
    renderThemes();
    drawCrop();
    try {
      const s = await api('/api/state');
      applyConfig(s);
      applyDevice(s.device);
      setConn('online', '已连接');
      syncTime();
    } catch (e) {
      applyConfig({});
      setConn('offline', '未连接');
      toast('未连接到胸牌');
    }
    try {
      state.avatar = { image: await loadImage('/avatar.jpg?' + Date.now()), zoom: 1, ox: 0, oy: 0 };
      drawCrop();
    } catch (e) { /* keep placeholder */ }
    drawPreview();
    setInterval(async () => {
      if (document.hidden || state.busy) return;
      try {
        const s = await api('/api/state', {}, 5000);
        applyDevice(s.device);
        setConn('online', '已连接');
        if (state.view !== 'scan') drawPreview();
      } catch (e) {
        setConn('offline', '未连接');
      }
    }, 6000);
  }

  // ------------------------------------------------------------ publish
  async function upload(name, blob) {
    if (!blob) throw new Error('render failed');
    return api('/api/assets', { method: 'POST', headers: { 'Content-Type': 'image/jpeg', 'X-Asset-Name': name }, body: blob }, 20000);
  }

  function layerCanvas(w, h) {
    const c = document.createElement('canvas');
    c.width = w;
    c.height = h;
    return c;
  }

  $('#publish').addEventListener('click', async () => {
    if (state.busy) return;
    const btn = $('#publish');
    const status = $('#pubStatus');
    const m = model();
    if (!m.name) {
      toast('请先填写姓名');
      $('#name').focus();
      return;
    }
    state.busy = true;
    btn.disabled = true;
    btn.setAttribute('aria-busy', 'true');
    status.className = 'pub-status';
    const step = text => { status.textContent = text; };
    try {
      const t = theme();
      const card = layerCanvas(C.SIZE, C.SIZE);
      C.drawCard(card.getContext('2d'), m, state.avatar, t);
      step('上传名片页 1/4');
      await upload('card', await canvasBlob(card, 0.93));

      if (m.about) {
        const about = layerCanvas(C.SIZE, C.SIZE);
        C.drawAbout(about.getContext('2d'), m, t);
        step('上传介绍页 2/4');
        await upload('about', await canvasBlob(about, 0.93));
      }

      const name = layerCanvas(360, 96);
      C.drawNameStrip(name.getContext('2d'), m);
      step('上传待机名字 3/4');
      await upload('name', await canvasBlob(name, 0.9));

      if (state.avatarDirty && state.avatar.image) {
        const a = layerCanvas(320, 320);
        const ctx = a.getContext('2d');
        const { image, zoom, ox, oy } = state.avatar;
        const iw = image.naturalWidth, ih = image.naturalHeight, side = Math.min(iw, ih) / zoom;
        ctx.drawImage(image, (iw - side) / 2 - ox * side, (ih - side) / 2 - oy * side, side, side, 0, 0, 320, 320);
        await upload('avatar', await canvasBlob(a, 0.9));
      }

      step('保存设置 4/4');
      await api('/api/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          ...m,
          theme: t.id,
          colors: { bg: t.bg, ink: t.ink, muted: t.muted, accent: t.accent },
          brightness: state.brightness,
          ambientBrightness: state.ambientBrightness,
          ambientAfter: state.ambientAfter,
          scanTimeout: state.scanTimeout,
          time: Math.floor(Date.now() / 1000),
          tz: -new Date().getTimezoneOffset(),
          publish: true,
        }),
      });
      state.dirty = false;
      state.avatarDirty = false;
      status.className = 'pub-status ok';
      status.textContent = '已发布';
      toast('发布成功');
    } catch (e) {
      status.className = 'pub-status err';
      status.textContent = `发布失败：${e.name === 'AbortError' ? '请求超时' : e.message}`;
    } finally {
      state.busy = false;
      btn.disabled = false;
      btn.removeAttribute('aria-busy');
    }
  });

  $('#resetBtn').addEventListener('click', async () => {
    if (!confirm('恢复默认会清空名片、二维码和图层，胸牌回到设置页。继续吗？')) return;
    try {
      await api('/api/reset', { method: 'POST' });
      location.reload();
    } catch (e) {
      toast('恢复失败，请重试');
    }
  });

  window.addEventListener('beforeunload', e => {
    if (state.dirty) e.preventDefault();
  });

  boot();
})();
