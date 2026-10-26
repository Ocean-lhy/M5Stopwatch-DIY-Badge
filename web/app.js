(function () {
  const $ = s => document.querySelector(s);
  const $$ = s => Array.from(document.querySelectorAll(s));
  const C = window.Compose;
  const TEXT_FIELDS = ['name', 'title', 'company', 'tags', 'email', 'phone'];
  const MAX_SLOTS = 3;

  const state = {
    view: 'card',
    theme: 0,
    brightness: 200,
    ambientBrightness: 40,
    ambientAfter: 45,
    ambientHold: 45,
    scanTimeout: 30,
    rotation: 0,
    avatar: { image: null, zoom: 1, ox: 0, oy: 0 },
    avatarDirty: false,
    battery: -1,
    runtime: -1,
    dirty: false,
    busy: false,
    scanIndex: 0,
  };

  const messages = {
    zh: {
      title: 'M5 Badge Studio',
      connecting: '连接中', connected: '已连接', offline: '未连接',
      previewAria: '胸牌屏幕预览', tabsAria: '预览页面',
      tabCard: '名片', tabScan: '扫码', tabAmbient: '待机',
      battery: '电量', hotspot: '热点',
      charging: '充电中',
      cardInfo: '名片信息', name: '姓名', namePh: '姓名',
      job: '职位', jobPh: '职位', company: '公司', companyPh: '公司',
      tags: '标签', tagsPh: '逗号分隔，最多 3 个',
      avatar: '头像', avatarHint: '拖动调整位置', cropAria: '头像裁剪',
      pickPhoto: '选择照片', zoom: '缩放',
      qr: '二维码', qrHint: '选择二维码截图自动识别，最多 3 个',
      addQr: '添加二维码', addVcard: '生成联系人二维码',
      email: '邮箱', phone: '电话', phonePh: '可选',
      wechatHint: '微信：我 → 个人资料 → 我的二维码 → “…” → 保存图片',
      look: '外观', themesAria: '主题',
      theme0: '深海', theme1: '品牌蓝', theme2: '纸白',
      brightness: '名片亮度', ambient: '进入待机', never: '从不', seconds: '{n} 秒',
      scanBack: '扫码页返回', rotation: '屏幕方向', rotationAria: '屏幕方向',
      keys: '按键', yellow: '黄键', yellowDo: '扫码页 / 返回',
      blue: '蓝键', blueDo: '扫码页切换二维码',
      blueLong: '长按蓝键 2 秒', blueLongDo: '设置模式',
      androidHint: '无法选择图片时，用浏览器打开',
      pickBlocked: '这是系统的 Wi-Fi 登录窗口，不能选择图片。关闭它并保持 Wi-Fi 连接（iPhone 点右上角“完成”，或“取消”→“不使用互联网”），再用浏览器打开：',
      captiveBanner: '当前在 Wi-Fi 登录窗口中，可以改文字和发布；换头像、识别截图请用浏览器打开 192.168.4.1。',
      copyUrl: '复制地址', copied: '已复制',
      reset: '恢复默认设置', publish: '发布到胸牌',
      dirty: '未发布', published: '已发布',
      qrSlot: '二维码 {n}', remove: '删除',
      showText: '显示文字', showTextPh: '微信扫一扫',
      qrText: '二维码内容', qrTextPh: '链接或文本', decode: '识别截图',
      decoding: '识别中…', decoded: '已识别',
      decodeMiss: '未识别到二维码', decodeLong: '内容太长，胸牌放不下',
      badImage: '无法读取这张图片，请换一张 JPG/PNG',
      needContact: '先填写邮箱或电话', vcardLabel: '扫码保存联系方式',
      noBadge: '未连接到胸牌', needName: '请先填写姓名',
      upCard: '上传名片页 1/3', upName: '上传待机名字 2/3', upSave: '保存设置 3/3',
      publishOk: '发布成功', publishFail: '发布失败：{msg}', timeout: '请求超时',
      resetConfirm: '恢复默认会清空名片、二维码和图层，胸牌回到设置页。继续吗？',
      resetFail: '恢复失败，请重试',
      previewName: '你的名字', noQr: '未设置二维码',
      powerTitle: '耗电',
      powerLeft: '剩余', powerMa: '电流', powerMv: '电压', powerUsed: '已用',
      legendHours: '剩余', legendVolts: '电压',
      calTitle: '校准',
      legendFactory: '出厂', legendLearned: '本机',
      powerEmpty: '暂无记录',
      powerReset: '清除校准',
      powerResetConfirm: '清除本机校准，容量回到 450 mAh？',
      powerResetOk: '已恢复默认',
      powerResetFail: '清除失败',
    },
    en: {
      title: 'M5 Badge Studio',
      connecting: 'Connecting', connected: 'Connected', offline: 'Offline',
      previewAria: 'Badge preview', tabsAria: 'Preview pages',
      tabCard: 'Card', tabScan: 'QR', tabAmbient: 'Idle',
      battery: 'Battery', hotspot: 'Hotspot',
      charging: 'Charging',
      cardInfo: 'Card', name: 'Name', namePh: 'Name',
      job: 'Title', jobPh: 'Title', company: 'Company', companyPh: 'Company',
      tags: 'Tags', tagsPh: 'Comma separated, up to 3',
      avatar: 'Photo', avatarHint: 'Drag to reposition', cropAria: 'Crop photo',
      pickPhoto: 'Choose photo', zoom: 'Zoom',
      qr: 'QR codes', qrHint: 'Pick a screenshot to read it. Up to 3.',
      addQr: 'Add QR code', addVcard: 'Make contact QR',
      email: 'Email', phone: 'Phone', phonePh: 'Optional',
      wechatHint: 'WeChat: Me → Profile → My QR Code → … → Save Image',
      look: 'Appearance', themesAria: 'Theme',
      theme0: 'Ocean', theme1: 'Brand', theme2: 'Paper',
      brightness: 'Card brightness', ambient: 'Idle after', never: 'Never', seconds: '{n} s',
      scanBack: 'QR page returns', rotation: 'Orientation', rotationAria: 'Orientation',
      keys: 'Buttons', yellow: 'Yellow', yellowDo: 'QR page / back',
      blue: 'Blue', blueDo: 'Next QR code',
      blueLong: 'Hold blue 2 s', blueLongDo: 'Setup mode',
      androidHint: 'If photos cannot be picked, open',
      pickBlocked: 'This is the system Wi-Fi sign-in window, which cannot pick photos. Close it and stay on the Wi-Fi (iPhone: Done, or Cancel → Use Without Internet), then open in a browser:',
      captiveBanner: 'You are in the Wi-Fi sign-in window. Text edits and publishing work here; to change the photo or read a screenshot, open 192.168.4.1 in a browser.',
      copyUrl: 'Copy address', copied: 'Copied',
      reset: 'Restore defaults', publish: 'Publish to badge',
      dirty: 'Not published', published: 'Published',
      qrSlot: 'QR {n}', remove: 'Remove',
      showText: 'Label', showTextPh: 'Scan to add me',
      qrText: 'QR content', qrTextPh: 'Link or text', decode: 'Read screenshot',
      decoding: 'Reading…', decoded: 'Read',
      decodeMiss: 'No QR code found', decodeLong: 'Too long for the badge',
      badImage: 'Could not read this image. Use a JPG or PNG.',
      needContact: 'Add an email or phone first', vcardLabel: 'Save contact',
      noBadge: 'Not connected to the badge', needName: 'Enter a name first',
      upCard: 'Uploading card 1/3', upName: 'Uploading idle name 2/3', upSave: 'Saving settings 3/3',
      publishOk: 'Published', publishFail: 'Publish failed: {msg}', timeout: 'Request timed out',
      resetConfirm: 'Restore defaults clears the card, QR codes, and layers, and the badge returns to setup. Continue?',
      resetFail: 'Could not restore defaults. Try again.',
      previewName: 'Your name', noQr: 'No QR code',
      powerTitle: 'Power',
      powerLeft: 'Left', powerMa: 'Current', powerMv: 'Voltage', powerUsed: 'Used',
      legendHours: 'Left', legendVolts: 'Voltage',
      calTitle: 'Calibration',
      legendFactory: 'Factory', legendLearned: 'Device',
      powerEmpty: 'No data',
      powerReset: 'Clear calibration',
      powerResetConfirm: 'Clear the device curve and return to 450 mAh?',
      powerResetOk: 'Restored',
      powerResetFail: 'Could not clear',
    },
  };
  let lang = localStorage.getItem('m5-lang');
  if (lang !== 'zh' && lang !== 'en') lang = navigator.language.toLowerCase().startsWith('zh') ? 'zh' : 'en';
  const tr = (key, vars = {}) => Object.entries(vars).reduce(
    (s, [k, v]) => s.replaceAll(`{${k}}`, v), messages[lang][key] ?? messages.en[key] ?? key);
  let connKind = 'pending';
  let pubKey = '';
  let pubVars = {};
  let lastDevice = null;

  function applyI18n(root) {
    const scope = root || document;
    scope.querySelectorAll('[data-i18n]').forEach(el => { el.textContent = tr(el.dataset.i18n); });
    scope.querySelectorAll('[data-i18n-placeholder]').forEach(el => { el.placeholder = tr(el.dataset.i18nPlaceholder); });
    scope.querySelectorAll('[data-i18n-aria]').forEach(el => { el.setAttribute('aria-label', tr(el.dataset.i18nAria)); });
  }

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

  function setConn(kind) {
    connKind = kind;
    $('#conn').className = 'conn ' + (kind === 'pending' ? '' : kind);
    $('#connText').textContent = tr(kind === 'online' ? 'connected' : kind === 'offline' ? 'offline' : 'connecting');
  }

  function setPub(key, vars = {}) {
    pubKey = key || '';
    pubVars = vars;
    const s = $('#pubStatus');
    s.className = 'pub-status' + (key === 'dirty' ? ' dirty' : key === 'published' ? ' ok' : key === 'publishFail' ? ' err' : '');
    s.textContent = key ? tr(key, vars) : '';
  }

  function markDirty() {
    state.dirty = true;
    setPub('dirty');
    schedulePreview();
  }

  // ------------------------------------------------------------ model
  function model() {
    const m = {};
    for (const id of TEXT_FIELDS) m[id] = $('#' + id).value.trim();
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
    $('#preview').style.transform = `rotate(${(state.rotation & 3) * 90}deg)`;
    const m = model();
    const t = theme();
    preview.save();
    if (state.view === 'card') {
      C.drawCard(preview, m, state.avatar, t);
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
  // iOS sign-in sheets have no "Safari/" token; Android sign-in apps run in a WebView ("; wv").
  function inSignInWindow() {
    const ua = navigator.userAgent;
    if (/iPhone|iPad|iPod/.test(ua)) return !/Safari\//.test(ua) && !/CriOS|FxiOS|EdgiOS|MicroMessenger/.test(ua);
    return /Android/.test(ua) && /; wv\)/.test(ua);
  }
  const signInWindow = inSignInWindow();

  function showPickHelp(anchor) {
    const host = anchor.closest('.avatar-tools, .slot-tools') || anchor.parentElement;
    if (host.querySelector('.pick-help')) return;
    const url = `http://${location.host || '192.168.4.1'}/`;
    const box = document.createElement('div');
    box.className = 'notice warn pick-help';
    box.innerHTML = '<p></p><div class="pick-url"><input class="mono" readonly><button type="button" class="ghost small"></button></div>';
    box.querySelector('p').textContent = tr('pickBlocked');
    const field = box.querySelector('input');
    field.value = url;
    const copy = box.querySelector('button');
    copy.textContent = tr('copyUrl');
    copy.addEventListener('click', async () => {
      field.select();
      try { await navigator.clipboard.writeText(url); } catch (e) { document.execCommand && document.execCommand('copy'); }
      copy.textContent = tr('copied');
    });
    host.appendChild(box);
  }

  // Sign-in windows silently ignore file pickers; explain instead of doing nothing.
  function openPicker(input, anchor) {
    if (signInWindow) {
      showPickHelp(anchor);
      return;
    }
    let reacted = false;
    const mark = () => { reacted = true; };
    window.addEventListener('blur', mark, { once: true });
    document.addEventListener('visibilitychange', mark, { once: true });
    input.addEventListener('change', mark, { once: true });
    input.addEventListener('cancel', mark, { once: true });
    input.click();
    // Android pickers always background the page, so no reaction means a blocked
    // picker (e.g. a vendor sign-in app the UA check missed). iOS shows an in-page
    // sheet without blur, so it relies on the UA check alone.
    if (!/Android/.test(navigator.userAgent)) return;
    setTimeout(() => {
      window.removeEventListener('blur', mark);
      document.removeEventListener('visibilitychange', mark);
      if (!reacted) showPickHelp(anchor);
    }, 1500);
  }

  $('#avatarPick').addEventListener('click', e => openPicker($('#avatarFile'), e.currentTarget));
  $('#avatarFile').addEventListener('change', async e => {
    const file = e.target.files[0];
    if (!file) return;
    try {
      const url = URL.createObjectURL(file);
      state.avatar = { image: await loadImage(url), zoom: 1, ox: 0, oy: 0 };
      $('#zoom').value = 1;
      avatarChanged();
    } catch (err) {
      toast(tr('badImage'));
    }
  });

  // ------------------------------------------------------------ QR slots
  function slots() {
    return $$('#slots .slot');
  }

  function renumberSlots() {
    slots().forEach((s, i) => { s.querySelector('.slot-title').textContent = tr('qrSlot', { n: i + 1 }); });
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
    const file = node.querySelector('.s-file');
    node.querySelector('.pick').addEventListener('click', e => openPicker(file, e.currentTarget));
    file.addEventListener('change', e => recognize(node, e.target));
    $('#slots').appendChild(node);
    applyI18n(node);
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

  function setSlotStatus(node, key) {
    node.dataset.status = key || '';
    const status = node.querySelector('.s-status');
    const err = key === 'decodeMiss' || key === 'decodeLong';
    status.className = 's-status' + (key === 'decoded' ? ' ok' : key === 'decoding' ? ' busy' : err ? ' err' : '');
    status.textContent = key ? tr(key) : '';
  }

  async function recognize(node, input) {
    const file = input.files[0];
    if (!file) return;
    setSlotStatus(node, 'decoding');
    try {
      const img = await loadImage(URL.createObjectURL(file));
      const text = await decodeQr(img);
      if (!text) throw new Error('not found');
      if (text.length > 250) throw new Error('too long');
      node.querySelector('.s-text').value = text;
      const label = node.querySelector('.s-label');
      if (!label.value && /wechat\.com|weixin/i.test(text)) label.value = tr('showTextPh');
      setSlotStatus(node, 'decoded');
      state.view = 'scan';
      state.scanIndex = slots().indexOf(node);
      $$('#previewTabs button').forEach(x => x.setAttribute('aria-selected', String(x.dataset.view === 'scan')));
      markDirty();
    } catch (e) {
      setSlotStatus(node, e.message === 'too long' ? 'decodeLong' : 'decodeMiss');
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
      toast(tr('needContact'));
      $('#email').focus();
      return;
    }
    addSlot(tr('vcardLabel'), vcard());
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
      b.innerHTML = `<i style="background:${t.bg};--dot:${t.accent}"></i><span></span>`;
      b.querySelector('span').textContent = tr('theme' + t.id);
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
  const syncRotation = bindSeg('rotation', 'rotation');

  function syncAmbient() {
    const never = state.ambientAfter === 0;
    $('#ambientNever').checked = never;
    $('#ambientAfter').disabled = never;
    $('#ambientOut').textContent = never ? tr('never') : tr('seconds', { n: state.ambientAfter });
  }
  function syncScan() {
    $('#scanTimeout').value = state.scanTimeout;
    $('#scanOut').textContent = tr('seconds', { n: state.scanTimeout });
  }
  $('#ambientAfter').addEventListener('input', e => {
    state.ambientAfter = Number(e.target.value);
    state.ambientHold = state.ambientAfter;
    syncAmbient();
    markDirty();
  });
  $('#ambientNever').addEventListener('change', e => {
    if (e.target.checked) {
      state.ambientHold = Number($('#ambientAfter').value) || state.ambientHold || 45;
      state.ambientAfter = 0;
    } else {
      state.ambientAfter = state.ambientHold || 45;
    }
    syncAmbient();
    markDirty();
  });
  $('#scanTimeout').addEventListener('input', e => {
    state.scanTimeout = Number(e.target.value);
    syncScan();
    markDirty();
  });

  $('#brightness').addEventListener('input', e => {
    state.brightness = Number(e.target.value);
    $('#brightnessOut').textContent = e.target.value;
    markDirty();
  });
  TEXT_FIELDS.forEach(id => $('#' + id).addEventListener('input', markDirty));

  // ------------------------------------------------------------ device state
  function applyDevice(d) {
    if (!d) return;
    lastDevice = d;
    state.battery = typeof d.battery === 'number' ? d.battery : -1;
    state.runtime = !d.charging && typeof d.runtime === 'number' ? d.runtime : -1;
    const runtime = state.runtime >= 0 ? ` · ${state.runtime.toFixed(1)}h` : '';
    $('#devBattery').textContent = state.battery >= 0 ? `${state.battery}%${d.charging ? ' ' + tr('charging') : runtime}` : '--';
    $('#devSsid').textContent = d.ssid || '--';
  }

  let powerData = null;

  function cssColor(name, fallback) {
    const v = getComputedStyle(document.documentElement).getPropertyValue(name).trim();
    return v || fallback;
  }

  function fitCanvas(canvas, cssH) {
    const dpr = Math.min(2, window.devicePixelRatio || 1);
    const w = canvas.clientWidth || 320;
    canvas.width = Math.max(1, Math.round(w * dpr));
    canvas.height = Math.max(1, Math.round(cssH * dpr));
    const ctx = canvas.getContext('2d');
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    return { ctx, w, h: cssH };
  }

  function clockLabel(t) {
    if (!(t > 1e9)) return '';
    return new Date(t * 1000).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
  }

  function drawSeries(ctx, samples, xOf, yOf, color, dash) {
    ctx.beginPath();
    ctx.strokeStyle = color;
    ctx.lineWidth = 1.75;
    ctx.setLineDash(dash ? [4, 3] : []);
    let pen = false;
    samples.forEach((s, i) => {
      const y = yOf(s, i);
      if (y == null) { pen = false; return; }
      const x = xOf(s, i);
      if (!pen) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
      pen = true;
    });
    ctx.stroke();
    ctx.setLineDash([]);
  }

  function drawPowerChart(p) {
    const canvas = $('#powerChart');
    if (!canvas) return;
    const { ctx, w, h } = fitCanvas(canvas, 200);
    ctx.clearRect(0, 0, w, h);
    const muted = cssColor('--muted', '#73808a');
    const samples = (p && p.samples) || [];
    if (samples.length < 2) {
      ctx.fillStyle = muted;
      ctx.font = '13px sans-serif';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      ctx.fillText(tr('powerEmpty'), w / 2, h / 2);
      return;
    }
    const pad = { l: 34, r: 38, t: 14, b: 22 };
    const plotW = Math.max(1, w - pad.l - pad.r);
    const plotH = Math.max(1, h - pad.t - pad.b);
    const timed = samples.filter(s => s.t > 1e9).length > samples.length / 2;
    const x0 = timed ? samples.find(s => s.t > 1e9).t : 0;
    const x1 = timed ? samples[samples.length - 1].t : samples.length - 1;
    const span = Math.max(1, x1 - x0);
    const xOf = (s, i) => pad.l + (((timed ? (s.t > 1e9 ? s.t : x0) : i) - x0) / span) * plotW;
    let maxH = 2;
    samples.forEach(s => { if (!s.charging && s.h > maxH) maxH = s.h; });
    maxH *= 1.08;
    const yH = v => pad.t + (1 - Math.min(Math.max(v, 0), maxH) / maxH) * plotH;
    const yV = mv => pad.t + (1 - (Math.min(4300, Math.max(3400, mv)) - 3400) / 900) * plotH;
    ctx.strokeStyle = cssColor('--line', '#d8e2eb');
    ctx.lineWidth = 1;
    ctx.beginPath();
    for (let g = 0; g <= 4; g++) {
      const y = pad.t + (g / 4) * plotH;
      ctx.moveTo(pad.l, y);
      ctx.lineTo(w - pad.r, y);
    }
    ctx.stroke();
    ctx.fillStyle = muted;
    ctx.font = '11px sans-serif';
    ctx.textAlign = 'right';
    ctx.textBaseline = 'middle';
    ctx.fillText(`${maxH.toFixed(0)}h`, pad.l - 4, pad.t + 4);
    ctx.fillText('0', pad.l - 4, pad.t + plotH);
    ctx.textAlign = 'left';
    ctx.fillText('4.3V', w - pad.r + 4, pad.t + 4);
    ctx.fillText('3.4V', w - pad.r + 4, pad.t + plotH);
    drawSeries(ctx, samples, xOf, s => (s.charging ? null : yH(s.h)), cssColor('--blue', '#087ac1'), false);
    drawSeries(ctx, samples, xOf, s => yV(s.mv), cssColor('--ink-2', '#526574'), false);
    ctx.fillStyle = muted;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'alphabetic';
    for (let i = 0; i <= 2; i++) {
      const idx = Math.round((samples.length - 1) * i / 2);
      const s = samples[idx];
      ctx.fillText(timed && s.t > 1e9 ? clockLabel(s.t) : `${idx}`, xOf(s, idx), h - 4);
    }
  }

  function drawCalChart(p) {
    const canvas = $('#calChart');
    if (!canvas || !p) return;
    const { ctx, w, h } = fitCanvas(canvas, 168);
    ctx.clearRect(0, 0, w, h);
    const pad = { l: 36, r: 12, t: 14, b: 22 };
    const plotW = Math.max(1, w - pad.l - pad.r);
    const plotH = Math.max(1, h - pad.t - pad.b);
    const cap = p.capacity_mah > 0 ? p.capacity_mah : 450;
    const top = cap * 1.05;
    const xOfMv = mv => pad.l + ((4300 - Math.min(4300, Math.max(3400, mv))) / 900) * plotW;
    const yOfUsed = used => pad.t + (1 - Math.min(Math.max(used, 0), top) / top) * plotH;
    const muted = cssColor('--muted', '#73808a');
    ctx.strokeStyle = cssColor('--line', '#d8e2eb');
    ctx.lineWidth = 1;
    ctx.beginPath();
    for (let g = 0; g <= 4; g++) {
      const y = pad.t + (g / 4) * plotH;
      ctx.moveTo(pad.l, y);
      ctx.lineTo(w - pad.r, y);
    }
    ctx.stroke();
    ctx.fillStyle = muted;
    ctx.font = '11px sans-serif';
    ctx.textAlign = 'right';
    ctx.textBaseline = 'middle';
    ctx.fillText(String(Math.round(cap)), pad.l - 4, pad.t + 4);
    ctx.fillText('0', pad.l - 4, pad.t + plotH);
    ctx.textAlign = 'center';
    ctx.textBaseline = 'alphabetic';
    ctx.fillText('4.3V', pad.l, h - 4);
    ctx.fillText('3.4V', w - pad.r, h - 4);
    const factory = (p.factory_curve || []).map(pt => ({ mv: pt.mv, used: cap * (1 - pt.pct / 100) }));
    if (factory.length >= 2) {
      drawSeries(ctx, factory, s => xOfMv(s.mv), s => yOfUsed(s.used), cssColor('--ink-2', '#526574'), true);
    }
    const learned = p.learned_curve || [];
    if (learned.length >= 2) {
      drawSeries(ctx, learned, s => xOfMv(s.mv), s => yOfUsed(s.used), cssColor('--blue', '#087ac1'), false);
      ctx.fillStyle = cssColor('--blue', '#087ac1');
      learned.forEach(s => {
        ctx.beginPath();
        ctx.arc(xOfMv(s.mv), yOfUsed(s.used), 2.5, 0, Math.PI * 2);
        ctx.fill();
      });
    }
  }

  function renderPower(p) {
    if (!p) return;
    powerData = p;
    const runtime = $('#pwrRuntime');
    const unit = $('#pwrRuntimeUnit');
    if (!runtime) return;
    if (p.charging || !(p.runtime >= 0)) {
      runtime.textContent = p.charging ? tr('charging') : '--';
      if (unit) unit.hidden = true;
    } else {
      runtime.textContent = p.runtime >= 99.5 ? '99+' : p.runtime.toFixed(1);
      if (unit) unit.hidden = false;
    }
    $('#pwrMa').textContent = p.ma >= 0 ? Math.round(p.ma) : '--';
    $('#pwrMv').textContent = p.mv > 0 ? (p.mv / 1000).toFixed(2) : '--';
    $('#pwrUsed').textContent = p.used_mah >= 0 ? Math.round(p.used_mah) : '--';
    $('#pwrCapTag').textContent = `${Math.round(p.capacity_mah || 450)} mAh`;
    const learned = $('#legendLearned');
    if (learned) learned.hidden = !((p.learned_curve || []).length >= 2);
    drawPowerChart(p);
    drawCalChart(p);
  }

  async function refreshPower() {
    try { renderPower(await api('/api/power', {}, 5000)); } catch (e) { /* keep the last curve */ }
  }

  $('#powerReset').addEventListener('click', async () => {
    if (!confirm(tr('powerResetConfirm'))) return;
    const btn = $('#powerReset');
    btn.disabled = true;
    try {
      await api('/api/power/reset', { method: 'POST' });
      await refreshPower();
      try { applyDevice((await api('/api/state', {}, 5000)).device); } catch (e) { /* power card is already refreshed */ }
      toast(tr('powerResetOk'));
    } catch (e) {
      toast(tr('powerResetFail'));
    } finally {
      btn.disabled = false;
    }
  });

  window.addEventListener('resize', () => { if (powerData) renderPower(powerData); });

  // Fills the form once; later polls only refresh telemetry so typing is never overwritten.
  function applyConfig(s) {
    for (const id of TEXT_FIELDS) $('#' + id).value = s[id] || '';
    state.theme = s.theme || 0;
    state.brightness = s.brightness || 200;
    state.ambientBrightness = s.ambientBrightness ?? 40;
    let ambient = s.ambientAfter ?? 45;
    if (ambient !== 0) ambient = Math.min(60, Math.max(10, ambient));
    state.ambientAfter = ambient;
    state.ambientHold = ambient || 45;
    if (ambient) $('#ambientAfter').value = ambient;
    let scan = s.scanTimeout || 30;
    state.scanTimeout = Math.min(30, Math.max(10, scan));
    state.rotation = [0, 1, 2, 3].includes(s.rotation) ? s.rotation : 0;
    $('#brightness').value = state.brightness;
    $('#brightnessOut').textContent = state.brightness;
    $('#slots').innerHTML = '';
    (s.qr || []).forEach(q => addSlot(q.label, q.text));
    if (!slots().length) addSlot();
    renderThemes();
    syncAmbient();
    syncScan();
    syncRotation();
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
    if (signInWindow) {
      const banner = document.createElement('p');
      banner.className = 'notice warn';
      banner.dataset.i18n = 'captiveBanner';
      banner.textContent = tr('captiveBanner');
      $('.steps').prepend(banner);
    }
    try {
      const s = await api('/api/state');
      applyConfig(s);
      applyDevice(s.device);
      setConn('online');
      syncTime();
      refreshPower();
    } catch (e) {
      applyConfig({});
      setConn('offline');
      toast(tr('noBadge'));
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
        setConn('online');
        if (state.view !== 'scan') drawPreview();
        refreshPower();
      } catch (e) {
        setConn('offline');
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
      toast(tr('needName'));
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
      step(tr('upCard'));
      await upload('card', await canvasBlob(card, 0.93));

      const name = layerCanvas(360, 96);
      C.drawNameStrip(name.getContext('2d'), m);
      step(tr('upName'));
      await upload('name', await canvasBlob(name, 0.9));

      if (state.avatarDirty && state.avatar.image) {
        const a = layerCanvas(320, 320);
        const ctx = a.getContext('2d');
        const { image, zoom, ox, oy } = state.avatar;
        const iw = image.naturalWidth, ih = image.naturalHeight, side = Math.min(iw, ih) / zoom;
        ctx.drawImage(image, (iw - side) / 2 - ox * side, (ih - side) / 2 - oy * side, side, side, 0, 0, 320, 320);
        await upload('avatar', await canvasBlob(a, 0.9));
      }

      step(tr('upSave'));
      await api('/api/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          ...m,
          about: false,
          touch: false,
          theme: t.id,
          colors: { bg: t.bg, ink: t.ink, muted: t.muted, accent: t.accent },
          brightness: state.brightness,
          ambientBrightness: state.ambientBrightness,
          ambientAfter: state.ambientAfter,
          scanTimeout: state.scanTimeout,
          rotation: state.rotation,
          time: Math.floor(Date.now() / 1000),
          tz: -new Date().getTimezoneOffset(),
          publish: true,
        }),
      });
      state.dirty = false;
      state.avatarDirty = false;
      setPub('published');
      toast(tr('publishOk'));
    } catch (e) {
      const msg = e.name === 'AbortError' ? tr('timeout') : e.message;
      setPub('publishFail', { msg });
      toast(tr('publishFail', { msg }));
    } finally {
      state.busy = false;
      btn.disabled = false;
      btn.removeAttribute('aria-busy');
    }
  });

  $('#resetBtn').addEventListener('click', async () => {
    if (!confirm(tr('resetConfirm'))) return;
    try {
      await api('/api/reset', { method: 'POST' });
      location.reload();
    } catch (e) {
      toast(tr('resetFail'));
    }
  });

  window.addEventListener('beforeunload', e => {
    if (state.dirty) e.preventDefault();
  });

  function applyLanguage() {
    document.documentElement.lang = lang === 'zh' ? 'zh-CN' : 'en';
    document.title = tr('title');
    applyI18n(document);
    $$('#lang button').forEach(b => {
      const on = b.dataset.lang === lang;
      b.setAttribute('aria-checked', String(on));
      b.classList.toggle('on', on);
    });
    if (C.labels) {
      C.labels.name = tr('previewName');
      C.labels.noQr = tr('noQr');
    }
    renderThemes();
    renumberSlots();
    $$('#slots .slot').forEach(node => { if (node.dataset.status) setSlotStatus(node, node.dataset.status); });
    syncAmbient();
    syncScan();
    setConn(connKind);
    if (pubKey) setPub(pubKey, pubVars);
    if (lastDevice) applyDevice(lastDevice);
    if (powerData) renderPower(powerData);
    schedulePreview();
  }

  $$('#lang button').forEach(b => b.addEventListener('click', () => {
    if (b.dataset.lang === lang) return;
    lang = b.dataset.lang === 'en' ? 'en' : 'zh';
    localStorage.setItem('m5-lang', lang);
    applyLanguage();
  }));

  try {
    applyLanguage();
  } catch (e) {
    console.error(e);
  }
  boot();
})();
