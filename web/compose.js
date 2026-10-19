// Renders the badge layers. The same functions draw the live preview and the
// JPEG layers uploaded to the device, so the preview is exactly what ships.
(function () {
  const S = 466, C = 233, R = 233;
  const FONT = '"PingFang SC","HarmonyOS Sans SC","Noto Sans SC","Microsoft YaHei",-apple-system,"Segoe UI",sans-serif';

  // Keep in sync with main/ui_theme.h.
  const labels = { name: '你的名字', noQr: '未设置二维码' };
  const THEMES = [
    { id: 0, name: '深海', bg: '#000000', ink: '#F2F6FA', muted: '#8A9BB0', accent: '#3BA7FF', glow: null },
    { id: 1, name: '品牌蓝', bg: '#032447', ink: '#FFFFFF', muted: '#A0C4E6', accent: '#7FD0FF', glow: 'rgba(8,122,193,.55)' },
    { id: 2, name: '纸白', bg: '#F4F1EA', ink: '#1A1A1A', muted: '#6E6A62', accent: '#E0542E', glow: null },
  ];

  const font = (size, weight = 400) => `${weight} ${size}px ${FONT}`;

  // Half-width of the round glass at a given y, minus a margin.
  function chord(y, margin = 0) {
    const d = Math.abs(y - C);
    return d >= R ? 0 : Math.max(0, Math.sqrt(R * R - d * d) - margin);
  }

  function fitFont(ctx, text, maxWidth, start, min, weight) {
    let size = start;
    for (; size > min; size -= 1) {
      ctx.font = font(size, weight);
      if (ctx.measureText(text).width <= maxWidth) return size;
    }
    ctx.font = font(min, weight);
    return min;
  }

  function ellipsize(ctx, text, maxWidth) {
    if (ctx.measureText(text).width <= maxWidth) return text;
    const chars = Array.from(text);
    while (chars.length && ctx.measureText(chars.join('') + '…').width > maxWidth) chars.pop();
    return chars.join('') + '…';
  }

  // Greedy wrap that understands CJK (break anywhere) and Latin words.
  function wrap(ctx, text, widthAt, maxLines) {
    const tokens = text.match(/[\u2e80-\u9fff\uff00-\uffef]|[^\s\u2e80-\u9fff\uff00-\uffef]+|\s+/g) || [];
    const lines = [];
    let line = '';
    let i = 0;
    for (; i < tokens.length; i++) {
      const tok = tokens[i];
      const next = line + tok;
      if (ctx.measureText(next.trim()).width <= widthAt(lines.length) || !line.trim()) {
        line = next;
        continue;
      }
      if (lines.length === maxLines - 1) break;
      lines.push(line.trim());
      line = tok.trim() ? tok : '';
    }
    if (line.trim()) {
      const rest = i < tokens.length ? line + tokens.slice(i).join('') : line;
      lines.push(ellipsize(ctx, rest.trim(), widthAt(lines.length)));
    }
    return lines;
  }

  // The device's JPEG decoder dithers to RGB565, so glows are only used where the
  // background is bright enough for the dither to stay invisible.
  function background(ctx, t) {
    ctx.fillStyle = t.bg;
    ctx.fillRect(0, 0, S, S);
    if (!t.glow) return;
    const g = ctx.createRadialGradient(C, 120, 10, C, 120, 300);
    g.addColorStop(0, t.glow);
    g.addColorStop(1, 'rgba(0,0,0,0)');
    ctx.fillStyle = g;
    ctx.fillRect(0, 0, S, S);
  }

  function roundRect(ctx, x, y, w, h, r) {
    ctx.beginPath();
    ctx.moveTo(x + r, y);
    ctx.arcTo(x + w, y, x + w, y + h, r);
    ctx.arcTo(x + w, y + h, x, y + h, r);
    ctx.arcTo(x, y + h, x, y, r);
    ctx.arcTo(x, y, x + w, y, r);
    ctx.closePath();
  }

  function tagsOf(model) {
    return (model.tags || '').split(/[,，、]/).map(s => s.trim()).filter(Boolean).slice(0, 3);
  }

  function drawAvatar(ctx, avatar, cx, cy, r, t) {
    ctx.save();
    ctx.beginPath();
    ctx.arc(cx, cy, r, 0, Math.PI * 2);
    ctx.clip();
    if (avatar && avatar.image) {
      const { image, zoom = 1, ox = 0, oy = 0 } = avatar;
      const side = Math.min(image.naturalWidth || image.width, image.naturalHeight || image.height) / zoom;
      const iw = image.naturalWidth || image.width, ih = image.naturalHeight || image.height;
      const sx = (iw - side) / 2 - ox * side, sy = (ih - side) / 2 - oy * side;
      ctx.drawImage(image, sx, sy, side, side, cx - r, cy - r, r * 2, r * 2);
    } else {
      ctx.fillStyle = t.accent;
      ctx.globalAlpha = 0.18;
      ctx.fillRect(cx - r, cy - r, r * 2, r * 2);
      ctx.globalAlpha = 1;
    }
    ctx.restore();
    ctx.lineWidth = 2;
    ctx.strokeStyle = t.accent;
    ctx.beginPath();
    ctx.arc(cx, cy, r + 7, 0, Math.PI * 2);
    ctx.stroke();
  }

  function drawCard(ctx, model, avatar, t) {
    background(ctx, t);
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    drawAvatar(ctx, avatar, C, 150, 72, t);

    const name = model.name || labels.name;
    fitFont(ctx, name, 340, 46, 26, 700);
    ctx.fillStyle = t.ink;
    ctx.fillText(name, C, 272);

    const sub = [model.title, model.company].filter(Boolean).join(' · ');
    if (sub) {
      fitFont(ctx, sub, 360, 21, 16, 400);
      ctx.fillStyle = t.muted;
      ctx.fillText(ellipsize(ctx, sub, 360), C, 312);
    }

    const tags = tagsOf(model);
    if (tags.length) {
      const y = 362;
      ctx.font = font(15, 500);
      const pad = 12, gap = 8, h = 28;
      let chips = tags.map(s => ({ s, w: ctx.measureText(s).width + pad * 2 }));
      while (chips.length && chips.reduce((a, c) => a + c.w, 0) + gap * (chips.length - 1) > 2 * chord(y, 26)) chips.pop();
      let x = C - (chips.reduce((a, c) => a + c.w, 0) + gap * (chips.length - 1)) / 2;
      for (const c of chips) {
        roundRect(ctx, x, y - h / 2, c.w, h, h / 2);
        ctx.globalAlpha = 0.14;
        ctx.fillStyle = t.accent;
        ctx.fill();
        ctx.globalAlpha = 1;
        ctx.fillStyle = t.ink;
        ctx.fillText(c.s, x + c.w / 2, y + 1);
        x += c.w + gap;
      }
    }
  }

  function drawAbout(ctx, model, t) {
    background(ctx, t);
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    const blocks = [];
    const intro = model.intro || '';
    ctx.font = font(24, 600);
    const lineH = 36;
    const introLines = intro ? wrap(ctx, intro, i => 2 * chord(150 + i * lineH, 40), 4) : [];
    blocks.push({ h: introLines.length * lineH, draw: y => {
      ctx.font = font(24, 600); ctx.fillStyle = t.ink;
      introLines.forEach((l, i) => ctx.fillText(l, C, y + lineH / 2 + i * lineH));
    } });

    if (model.seeking) {
      ctx.font = font(18, 400);
      const seekLines = wrap(ctx, model.seeking, () => 300, 2);
      blocks.push({ gap: 22, h: 4, draw: y => { ctx.fillStyle = t.accent; roundRect(ctx, C - 16, y, 32, 4, 2); ctx.fill(); } });
      blocks.push({ gap: 18, h: 18, draw: y => { ctx.font = font(13, 600); ctx.fillStyle = t.muted; ctx.fillText('我在找', C, y + 9); } });
      blocks.push({ gap: 6, h: seekLines.length * 27, draw: y => {
        ctx.font = font(18, 400); ctx.fillStyle = t.ink;
        seekLines.forEach((l, i) => ctx.fillText(l, C, y + 13 + i * 27));
      } });
    }
    if (model.booth) {
      const label = `展位 ${model.booth}`;
      blocks.push({ gap: 22, h: 34, draw: y => {
        ctx.font = font(16, 700);
        const w = ctx.measureText(label).width + 32;
        roundRect(ctx, C - w / 2, y, w, 34, 17);
        ctx.fillStyle = t.accent; ctx.fill();
        ctx.fillStyle = t.bg; ctx.fillText(label, C, y + 18);
      } });
    }
    const total = blocks.reduce((a, b, i) => a + b.h + (i ? b.gap || 0 : 0), 0);
    let y = Math.max(78, 240 - total / 2);
    blocks.forEach((b, i) => { if (i) y += b.gap || 0; b.draw(y); y += b.h; });
  }

  // 360x96 strip used on the dim ambient screen.
  function drawNameStrip(ctx, model) {
    ctx.fillStyle = '#000';
    ctx.fillRect(0, 0, 360, 96);
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    const name = model.name || '';
    fitFont(ctx, name, 340, 38, 22, 700);
    ctx.fillStyle = '#C9D2DC';
    ctx.fillText(name, 180, 36);
    const sub = [model.title, model.company].filter(Boolean).join(' · ');
    ctx.font = font(17, 400);
    ctx.fillStyle = '#6F7C8A';
    ctx.fillText(ellipsize(ctx, sub, 340), 180, 76);
  }

  // ----- previews of the natively drawn device pages -----

  function dotRing(ctx, remaining, lit, dim) {
    const n = 60, count = Math.ceil(remaining * n);
    for (let i = 0; i < n; i++) {
      const a = -Math.PI / 2 + (2 * Math.PI * i) / n;
      ctx.beginPath();
      ctx.arc(C + 222 * Math.cos(a), C + 222 * Math.sin(a), i < count ? 3 : 2, 0, Math.PI * 2);
      ctx.fillStyle = i < count ? lit : dim;
      ctx.fill();
    }
  }

  function drawQr(ctx, text, x, y, side) {
    if (!text || !window.qrcode) return false;
    let qr = null;
    for (let v = 1; v <= 20 && !qr; v++) {
      try { const q = window.qrcode(v, 'M'); q.addData(unescape(encodeURIComponent(text)), 'Byte'); q.make(); qr = q; } catch (e) { /* too long for v */ }
    }
    if (!qr) return false;
    const n = qr.getModuleCount();
    const m = Math.max(2, Math.floor(side / (n + 8)));
    const off = (side - n * m) / 2;
    ctx.fillStyle = '#000';
    for (let r = 0; r < n; r++) for (let c = 0; c < n; c++) if (qr.isDark(r, c)) ctx.fillRect(x + off + c * m, y + off + r * m, m, m);
    return true;
  }

  function drawScanPreview(ctx, model, t, index = 0) {
    ctx.fillStyle = '#000';
    ctx.fillRect(0, 0, S, S);
    dotRing(ctx, 0.72, t.accent, '#1C2836');
    const slots = (model.qr || []).filter(q => q.text);
    const slot = slots[index % Math.max(1, slots.length)];
    roundRect(ctx, C - 150, 64, 300, 300, 28);
    ctx.fillStyle = '#fff';
    ctx.fill();
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    if (!slot || !drawQr(ctx, slot.text, C - 150, 64, 300)) {
      ctx.font = font(16);
      ctx.fillStyle = '#5a5a5a';
      ctx.fillText(labels.noQr, C, 214);
    }
    if (slot && slot.label) {
      ctx.font = font(24, 700);
      ctx.fillStyle = '#fff';
      ctx.fillText(slot.label, C, 398);
    }
    if (slots.length > 1) {
      let x = C - ((slots.length - 1) * 18) / 2;
      slots.forEach((_, i) => {
        ctx.fillStyle = i === index % slots.length ? t.accent : '#46505C';
        if (i === index % slots.length) { roundRect(ctx, x - 8, 428, 16, 6, 3); ctx.fill(); }
        else { ctx.beginPath(); ctx.arc(x, 431, 3, 0, Math.PI * 2); ctx.fill(); }
        x += 18;
      });
    }
  }

  function runtimeText(runtime) {
    if (!(runtime >= 0)) return '';
    return runtime >= 99.5 ? '99h+' : `${runtime.toFixed(1)}h`;
  }

  // Battery glyph and runtime laid out from x; returns the width.
  function batteryBlock(ctx, x, cy, battery, runtime, color, measureOnly) {
    if (!(battery >= 0)) return 0;
    const text = runtimeText(runtime);
    ctx.font = font(18, 500);
    const tw = text ? ctx.measureText(text).width : 0;
    if (!measureOnly) {
      batteryGlyph(ctx, x, cy - 6, battery, color);
      if (text) { ctx.textAlign = 'left'; ctx.textBaseline = 'middle'; ctx.fillStyle = color; ctx.fillText(text, x + 34, cy); }
    }
    return 26 + (tw ? 8 + tw : 0);
  }

  function drawAmbientPreview(ctx, model, strip, battery, runtime) {
    ctx.fillStyle = '#000';
    ctx.fillRect(0, 0, S, S);
    const now = new Date();
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.font = `300 72px ${FONT}`;
    ctx.fillStyle = '#96A0AC';
    ctx.fillText(`${String(now.getHours()).padStart(2, '0')}:${String(now.getMinutes()).padStart(2, '0')}`, C, 186);
    ctx.drawImage(strip, C - 180, 236);
    const w = batteryBlock(ctx, 0, 358, battery, runtime, '#464C54', true);
    batteryBlock(ctx, C - w / 2, 358, battery, runtime, '#464C54', false);
    ctx.textAlign = 'center';
  }

  function batteryGlyph(ctx, x, y, pct, color) {
    ctx.strokeStyle = color;
    ctx.lineWidth = 1;
    roundRect(ctx, x + 0.5, y + 0.5, 24, 12, 3);
    ctx.stroke();
    ctx.fillStyle = color;
    ctx.fillRect(x + 24, y + 3, 2, 6);
    ctx.fillStyle = pct <= 10 ? '#FF453A' : pct <= 20 ? '#FF9F0A' : color;
    ctx.fillRect(x + 2, y + 2, Math.round((20 * pct) / 100), 8);
  }

  // Device status overlay on card/about, approximated for the preview.
  function drawStatusPreview(ctx, t, battery, runtime) {
    const now = new Date();
    const time = `${String(now.getHours()).padStart(2, '0')}:${String(now.getMinutes()).padStart(2, '0')}`;
    ctx.font = font(18, 500);
    const tw = ctx.measureText(time).width;
    const bw = batteryBlock(ctx, 0, 40, battery, runtime, t.muted, true);
    let x = C - (tw + (bw ? 12 + bw : 0)) / 2;
    ctx.textAlign = 'left';
    ctx.textBaseline = 'middle';
    ctx.fillStyle = t.muted;
    ctx.fillText(time, x, 40);
    batteryBlock(ctx, x + tw + 12, 40, battery, runtime, t.muted, false);
    ctx.textAlign = 'center';
  }

  window.Compose = { THEMES, labels, SIZE: S, drawCard, drawAbout, drawNameStrip, drawScanPreview, drawAmbientPreview, drawStatusPreview };
})();
