// Prueba de la interfaz de MINDROLL con un puente JUCE simulado: node tests/ui.test.mjs [ui/index.html] [/tmp/mo]
import { chromium } from '/opt/npm-tools/node_modules/playwright/index.mjs';
import path from 'node:path';
const file = 'file://' + path.resolve(process.argv[2] || 'ui/index.html');
const out = process.argv[3] || '/tmp/mo';
let fails = 0; const ok = (c, m) => { if (!c) { fails++; console.log('  FALLO: ' + m); } else console.log('  ok: ' + m); };

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 1038, height: 600 }, deviceScaleFactor: 2 });   // la parte web del plugin en un Mac Retina (la barra de arrastre es nativa)
const errors = []; page.on('pageerror', e => errors.push(e.message)); page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
await page.addInitScript(() => {
  window.__sent = []; window.__ls = {};
  window.__JUCE__ = { backend: { emitEvent: (e, o) => window.__sent.push([e, JSON.parse(JSON.stringify(o || {}))]), addEventListener: (e, f) => { window.__ls[e] = f; } } };
  window.__fire = (e, v) => window.__ls[e] && window.__ls[e](v);
});
await page.goto(file);
await page.waitForTimeout(300);
const sent = () => page.evaluate(() => window.__sent);
const clear = () => page.evaluate(() => { window.__sent = []; });
const fire = (e, v) => page.evaluate(([e, v]) => window.__fire(e, v), [e, v]);

ok((await sent()).some(([e]) => e === 'moReady'), 'avisa al plugin al arrancar (moReady)');
await fire('moSettings', { theme: 'dark', standalone: false });

// sin vocal
ok(await page.isVisible('#empty') && (await page.textContent('#empty b')) === 'DROP A SOUND HERE', 'sin sonido enseña "DROP A SOUND HERE"');
ok((await page.textContent('.srcp .ptitle')).startsWith('Sound'), 'el panel se llama SOUND');
ok(await page.isDisabled('#play') && await page.isDisabled('#stop'), 'PLAY y STOP apagados sin vocal');
ok(await page.isDisabled('#clear'), 'CLEAR apagado sin vocal');

// estado y plan desde el plugin (plan real de 2 compases EXPO)
const params = { len: 2, sstart: 0, send: 1, gate0: 0.35, gate1: 1, pitch: 5, filter: 0.5, reverse: false, sbpm: 124 };
await fire('moState', { p: params, cap: 0, prev: 0, pos: 0, bpm: 126 });
const line = [[0, 0.2], [0.55, 0.36], [1, 0.85]];
const hits = []; { let t = 0; while (t < 8) { const r = 0.5 * Math.pow(32, 0.2 + 0.65 * t / 8); const len = Math.min(1 / r, 8 - t); hits.push([t, len, 0.35 + 0.65 * t / 8, 5 * (t / 8) ** 2, 0, r]); t += len; } }
await fire('moPlan', { total: 8, bpm: 126, hits, use: 0.62, line });
const wave = Array.from({ length: 240 }, (_, i) => Math.min(1, i / 8) * Math.exp(-i / 120));
await fire('moSource', { loaded: true, name: 'hey_chop', dur: 0.8, wave });
ok(await page.isHidden('#empty'), 'con vocal se quita el aviso');
ok((await page.textContent('#sname')) === 'hey_chop', 'enseña el nombre del trozo');
ok((await page.textContent('#bpmt')) === '126', 'BPM del host');
ok((await page.textContent('#binfo')) === '2 BARS · ' + hits.length + ' HITS', 'resumen del build (' + await page.textContent('#binfo') + ')');
ok((await page.locator('#bars button.on').textContent()) === '2', 'LENGTH marca 2 compases');
ok((await page.locator('#shape button.on').textContent()) === 'EXPO', 'SHAPE reconoce la forma EXPO de la línea');
ok((await page.locator('.kv').nth(2).textContent()) === '+5 st', 'knob PITCH enseña +5 st');
ok(!(await page.isDisabled('#play')), 'PLAY activo con vocal');
ok(await page.isDisabled('#stop'), 'STOP apagado mientras no suena');
const lanePix = await page.evaluate(() => { const c = document.querySelector('#lcv'); const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data; let n = 0; for (let i = 3; i < d.length; i += 4) if (d[i] > 0) n++; return n; });
ok(lanePix > 2000, 'dibuja los golpes y la línea');

// nitidez: sin escalado a tamaño normal y lienzos a la resolución de la pantalla (Retina = 2x)
const crisp = await page.evaluate(() => { const w = document.querySelector('#wrap'), c = document.querySelector('#lcv');
  return { zoom: getComputedStyle(w).zoom, tf: getComputedStyle(w).transform, cw: c.width, ccw: c.clientWidth, dpr: devicePixelRatio }; });
ok(crisp.zoom === '1' && crisp.tf === 'none', 'a tamaño normal la interfaz no se escala (zoom ' + crisp.zoom + ', transform ' + crisp.tf + ')');
ok(crisp.cw === Math.round(crisp.ccw * crisp.dpr), 'la línea se dibuja a resolución Retina (' + crisp.cw + ' px para ' + crisp.ccw + ' px)');
await page.screenshot({ path: out + '-dark.png' });

// todos los botones de una fila con el mismo tamaño
const sizes = async sel => page.$$eval(sel, els => els.map(e => Math.round(e.getBoundingClientRect().width) + 'x' + Math.round(e.getBoundingClientRect().height)));
for (const sel of ['.btns3 button', '#bars button', '#shape button', '.row.end button.tr']) { const s = await sizes(sel); ok(new Set(s).size === 1, 'mismo tamaño en ' + sel + ' (' + s.join(', ') + ')'); }

// LENGTH y ROLL mandan el parámetro con su gesto
await clear();
await page.click('#bars button:nth-child(4)');
let s = await sent();
ok(s.some(([e, o]) => e === 'moSet' && o.id === 'len' && o.v === 8), 'LENGTH 8 manda len=8');
ok(s.some(([e, o]) => e === 'moGesture' && o.id === 'len' && o.begin), 'LENGTH abre el gesto (automatización)');
ok((await page.textContent('#lent')) === '8', 'la caja de LENGTH enseña 8');
await clear();
const lv = await page.locator('#lenv').boundingBox();
await page.mouse.move(lv.x + lv.width / 2, lv.y + lv.height / 2); await page.mouse.wheel(0, -100); await page.mouse.wheel(0, -100);
const lvs = (await sent()).filter(([e, o]) => e === 'moSet' && o.id === 'len').pop();
ok(lvs && lvs[1].v === 10, 'LENGTH libre: con la rueda pasa a 10 compases (' + (lvs && lvs[1].v) + ')');
ok((await page.locator('#bars button.on').count()) === 0, 'con 10 compases no se marca ningún atajo');
// SHAPE: LATE manda su línea
await clear();
await page.click('#shape button:nth-child(3)');
let ln = (await sent()).filter(([e]) => e === 'moLine').pop();
ok(ln && ln[1].pts.length === 4 && ln[1].pts[3][1] === 0.85, 'SHAPE LATE manda su línea al plugin');

// la línea: arrastrar un punto, clic para añadir, doble clic para quitar
const lb = await page.locator('#lcv').boundingBox();
const geoPt = await page.evaluate(() => { const c = document.querySelector('#lcv'), r = c.getBoundingClientRect(), w = c.clientWidth, h = c.clientHeight;
  // mismo cálculo que geo(): L=34, R=10, top=12, cb=h-46
  const L = 34, W = w - 44, top = 12, cb = h - 46, sx = r.width / w, sy = r.height / h;
  return { x: (x) => 0, L, W, top, cb, sx, sy, left: r.left, topPx: r.top }; });
const toPx = (x, y) => [geoPt.left + (geoPt.L + geoPt.W * x) * geoPt.sx, geoPt.topPx + (geoPt.top + (geoPt.cb - geoPt.top) * (1 - y)) * geoPt.sy];
await clear();
let [px, py] = toPx(0.7, 0.24);
await page.mouse.move(px, py); await page.mouse.down(); await page.mouse.move(px, py - 40, { steps: 5 }); await page.mouse.up();
ln = (await sent()).filter(([e]) => e === 'moLine').pop();
ok(ln && ln[1].pts.length === 4 && ln[1].pts[1][1] > 0.4, 'arrastrar un punto hacia arriba acelera esa parte (' + (ln && JSON.stringify(ln[1].pts[1])) + ')');
await clear();
[px, py] = toPx(0.3, 0.215);
await page.mouse.click(px, py);
ln = (await sent()).filter(([e]) => e === 'moLine').pop();
ok(ln && ln[1].pts.length === 5, 'clic en la línea añade un punto');
await clear();
await page.mouse.dblclick(px, py);
ln = (await sent()).filter(([e]) => e === 'moLine').pop();
ok(ln && ln[1].pts.length === 4, 'doble clic en un punto lo quita');
ok((await page.locator('#shape button.on').count()) === 0, 'con la línea editada no se marca ninguna forma');
await clear();
await page.keyboard.press('Control+z');
ok((await sent()).some(([e, o]) => e === 'moLine'), 'Cmd+Z deshace los cambios de la línea');

// knob GATE START: arrastrar hacia arriba
await clear();
const k = await page.locator('#k_gate0').boundingBox();
await page.mouse.move(k.x + k.width / 2, k.y + k.height / 2); await page.mouse.down();
await page.mouse.move(k.x + k.width / 2, k.y + k.height / 2 - 60, { steps: 6 }); await page.mouse.up();
s = await sent();
const g0 = s.filter(([e, o]) => e === 'moSet' && o.id === 'gate0').pop();
ok(g0 && g0[1].v > 0.35, 'GATE START sube al arrastrar (' + (g0 && g0[1].v) + ')');
ok(s.some(([e, o]) => e === 'moGesture' && o.id === 'gate0' && !o.begin), 'GATE START cierra el gesto');

// PITCH en semitonos enteros
await clear();
const kp = await page.locator('#k_pitch').boundingBox();
await page.mouse.move(kp.x + kp.width / 2, kp.y + kp.height / 2);
await page.mouse.wheel(0, -100);
const pv = (await sent()).filter(([e, o]) => e === 'moSet' && o.id === 'pitch').pop();
ok(pv && pv[1].v === 6, 'PITCH con la rueda sube 1 semitono (' + (pv && pv[1].v) + ')');

// REVERSE
await clear();
await page.click('#rev');
ok((await sent()).some(([e, o]) => e === 'moSet' && o.id === 'reverse' && o.v === 1), 'REVERSE se enciende');

// deshacer
await clear();
await page.keyboard.press('Control+z');
ok((await sent()).some(([e, o]) => e === 'moSet' && o.id === 'reverse' && o.v === 0), 'Cmd+Z deshace REVERSE');

// tiradores de la onda: elegir la parte del sonido
{
  const wb = await page.locator('#wcv').boundingBox();
  const cw = await page.evaluate(() => document.querySelector('#wcv').clientWidth);
  const fx = f => wb.x + (8 + (cw - 16) * f) * wb.width / cw;
  await clear();
  await page.mouse.move(fx(1), wb.y + wb.height / 2); await page.mouse.down(); await page.mouse.move(fx(0.5), wb.y + wb.height / 2, { steps: 6 }); await page.mouse.up();
  let ev = (await sent()).filter(([e, o]) => e === 'moSet' && o.id === 'send').pop();
  ok(ev && Math.abs(ev[1].v - 0.5) < 0.02, 'arrastrar el tirador derecho acorta el sonido (' + (ev && ev[1].v) + ')');
  await clear();
  await page.mouse.move(fx(0), wb.y + wb.height / 2); await page.mouse.down(); await page.mouse.move(fx(0.2), wb.y + wb.height / 2, { steps: 6 }); await page.mouse.up();
  ev = (await sent()).filter(([e, o]) => e === 'moSet' && o.id === 'sstart').pop();
  ok(ev && Math.abs(ev[1].v - 0.2) < 0.02, 'arrastrar el tirador izquierdo mueve el inicio (' + (ev && ev[1].v) + ')');
  await clear();
  await page.mouse.move(fx(0.35), wb.y + wb.height / 2); await page.mouse.down(); await page.mouse.move(fx(0.45), wb.y + wb.height / 2, { steps: 6 }); await page.mouse.up();
  const s1 = (await sent()).filter(([e, o]) => e === 'moSet' && o.id === 'sstart').pop(), e1 = (await sent()).filter(([e, o]) => e === 'moSet' && o.id === 'send').pop();
  ok(s1 && e1 && Math.abs(s1[1].v - 0.3) < 0.02 && Math.abs(e1[1].v - 0.6) < 0.02, 'arrastrar por el medio mueve el trozo entero (' + (s1 && s1[1].v) + ' a ' + (e1 && e1[1].v) + ')');
  await page.screenshot({ path: out + '-trim.png' });
  await clear();
  await page.mouse.dblclick(fx(0.45), wb.y + wb.height / 2);
  s = await sent();
  ok(s.some(([e, o]) => e === 'moSet' && o.id === 'sstart' && o.v === 0) && s.some(([e, o]) => e === 'moSet' && o.id === 'send' && o.v === 1), 'doble clic en la onda vuelve al sonido entero');
}

// espacio = PLAY/PAUSE (también después de haber pulsado un botón)
await page.click('#rev'); await page.click('#rev');
await clear();
await page.keyboard.press('Space');
s = (await sent()).map(([e]) => e);
ok(s.filter(e => e === 'moPlay').length === 1 && !s.includes('moSet'), 'la barra espaciadora hace PLAY sin volver a pulsar el último botón');

// botones del trozo y transporte
await clear();
await page.click('#cap'); await page.click('#play'); await page.click('#load'); await page.click('#clear');
s = (await sent()).map(([e]) => e);
ok(['moCapture', 'moPlay', 'moBrowse', 'moClear'].every(e => s.includes(e)), 'CAPTURE, PLAY, LOAD y CLEAR avisan al plugin (' + s.join(',') + ')');
await fire('moState', { cap: 1, prev: 1, pos: 0.5 });
ok((await page.textContent('#cap')) === 'WAITING', 'CAPTURE enseña WAITING mientras espera el tiempo');
ok((await page.textContent('#play span')) === 'PAUSE', 'PLAY pasa a PAUSE mientras suena');
ok(!(await page.isDisabled('#stop')), 'STOP activo mientras suena');
await page.screenshot({ path: out + '-playing.png' });
await clear(); await page.click('#stop');
ok((await sent()).some(([e]) => e === 'moStop'), 'STOP avisa al plugin');
await fire('moState', { cap: 2, prev: 2 });
ok((await page.textContent('#cap')) === 'RECORDING', 'CAPTURE enseña RECORDING');
ok((await page.textContent('#play span')) === 'PLAY' && !(await page.isDisabled('#stop')), 'en pausa: PLAY para seguir y STOP activo');
await fire('moState', { cap: 0, prev: 0 });

// soltar un archivo encima
await clear();
await page.evaluate(() => {
  const dt = new DataTransfer(); dt.items.add(new File([new Uint8Array([82, 73, 70, 70, 1, 2, 3])], 'vox.wav', { type: 'audio/wav' }));
  document.dispatchEvent(new DragEvent('drop', { dataTransfer: dt, bubbles: true, cancelable: true }));
});
await page.waitForTimeout(200);
const ld = (await sent()).find(([e]) => e === 'moLoad');
ok(ld && ld[1].name === 'vox.wav' && ld[1].data === 'UklGRgECAw==', 'soltar un WAV lo manda al plugin en base64');

// standalone: BPM arrastrable
await fire('moSettings', { standalone: true });
await clear();
const bb = await page.locator('#bpmv').boundingBox();
await page.mouse.move(bb.x + bb.width / 2, bb.y + bb.height / 2); await page.mouse.down();
await page.mouse.move(bb.x + bb.width / 2, bb.y + bb.height / 2 - 40, { steps: 4 }); await page.mouse.up();
const bpmS = (await sent()).filter(([e, o]) => e === 'moSet' && o.id === 'sbpm').pop();
ok(bpmS && bpmS[1].v > 124, 'en la app sola el BPM se arrastra (' + (bpmS && bpmS[1].v) + ')');
await fire('moSettings', { standalone: false });

// settings y tema claro
await page.click('#gear');
ok(await page.isVisible('#modal.show'), 'Settings se abre');
ok((await page.textContent('#ver')) === '0.3.0', 'Settings enseña la versión');
await clear();
await page.click('#theme button:nth-child(2)');
ok((await sent()).some(([e, o]) => e === 'moSettings' && o.theme === 'light'), 'el tema claro se guarda en el plugin');
await page.click('#mclose');
await page.screenshot({ path: out + '-light.png' });
await page.click('#gear'); await page.click('#theme button:nth-child(1)'); await page.click('#mclose');

// nada se sale de la ventana ni hay texto partido
const overflow = await page.evaluate(() => {
  const bad = [];
  document.querySelectorAll('#wrap button, #wrap .lbl, #wrap .kl, #wrap .chip, #wrap .hint').forEach(e => {
    const r = e.getBoundingClientRect(); if (r.width === 0) return;
    if (r.right > innerWidth - 19 || r.bottom > innerHeight - 19) bad.push(e.id || e.className + ':' + e.textContent);
    if (e.tagName === 'BUTTON' && e.scrollWidth > e.clientWidth + 1) bad.push('texto cortado: ' + e.textContent);
  });
  return bad;
});
ok(overflow.length === 0, 'nada se sale del marco ni se corta (' + overflow.join(' | ') + ')');
const noEmoji = await page.evaluate(() => !/[\u{1F300}-\u{1FAFF}\u{2600}-\u{27BF}]/u.test(document.body.innerText));
ok(noEmoji, 'sin emojis');
ok(errors.length === 0, 'sin errores de JavaScript (' + errors.join(' | ') + ')');

// también se ve bien más pequeña
await page.setViewportSize({ width: 800, height: 462 });
await page.waitForTimeout(150);
const small = await page.evaluate(() => { const c = document.querySelector('#lcv'); return { zoom: +getComputedStyle(document.querySelector('#wrap')).zoom, cw: c.width, ccw: c.clientWidth, dpr: devicePixelRatio }; });
ok(small.zoom < 1 && Math.abs(small.cw - Math.round(small.ccw * small.zoom * small.dpr)) <= 1, 'más pequeña usa zoom y los lienzos siguen a resolución real');
await page.screenshot({ path: out + '-small.png' });

await browser.close();
console.log(fails ? `\n${fails} FALLOS` : '\nTodo bien');
process.exit(fails ? 1 : 0);
