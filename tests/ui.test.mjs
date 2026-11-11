// Prueba de la interfaz de MINDROLL con un puente JUCE simulado: node tests/ui.test.mjs [ui/index.html] [/tmp/mo]
import { chromium } from '/opt/npm-tools/node_modules/playwright/index.mjs';
import path from 'node:path';
const file = 'file://' + path.resolve(process.argv[2] || 'ui/index.html');
const out = process.argv[3] || '/tmp/mo';
let fails = 0; const ok = (c, m) => { if (!c) { fails++; console.log('  FALLO: ' + m); } else console.log('  ok: ' + m); };

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 1038, height: 560 } });   // la parte web del plugin (la barra de arrastre es nativa)
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
ok(await page.isVisible('#empty'), 'sin vocal enseña "DROP A VOCAL HERE"');
ok(await page.isDisabled('#prev'), 'PREVIEW apagado sin vocal');
ok(await page.isDisabled('#clear'), 'CLEAR apagado sin vocal');

// estado y plan desde el plugin (plan real de 2 compases EXPO)
const params = { bars: 1, curve: 1, gate0: 0.35, gate1: 1, pitch: 5, filter: 0.5, reverse: false, sbpm: 124 };
await fire('moState', { p: params, cap: 0, prev: 0, pos: 0, bpm: 126 });
const hits = []; let t = 0;
[[0, 1, 4], [1, 0.5, 4], [2, 0.25, 4], [3, 0.125, 8]].forEach(([st, step, n]) => { for (let i = 0; i < n; i++) { hits.push([t, step, 0.35 + 0.65 * t / 8, 5 * (t / 8) ** 2, 0, st]); t += step; } });
await fire('moPlan', { total: 8, bpm: 126, hits, use: 0.62 });
const wave = Array.from({ length: 240 }, (_, i) => Math.min(1, i / 8) * Math.exp(-i / 120));
await fire('moSource', { loaded: true, name: 'hey_chop', dur: 0.8, wave });
ok(await page.isHidden('#empty'), 'con vocal se quita el aviso');
ok((await page.textContent('#sname')) === 'hey_chop', 'enseña el nombre del trozo');
ok((await page.textContent('#bpmt')) === '126', 'BPM del host');
ok((await page.textContent('#binfo')) === '2 BARS · 20 HITS', 'resumen del build (' + await page.textContent('#binfo') + ')');
ok((await page.locator('#bars button.on').textContent()) === '2', 'LENGTH marca 2 compases');
ok((await page.locator('#curve button.on').textContent()) === 'EXPO', 'ROLL marca EXPO');
ok((await page.locator('.kv').nth(2).textContent()) === '+5 st', 'knob PITCH enseña +5 st');
ok(!(await page.isDisabled('#prev')), 'PREVIEW activo con vocal');
const lanePix = await page.evaluate(() => { const c = document.querySelector('#lcv'); const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data; let n = 0; for (let i = 3; i < d.length; i += 4) if (d[i] > 0) n++; return n; });
ok(lanePix > 2000, 'dibuja los golpes en la línea del build');
await page.screenshot({ path: out + '-dark.png' });

// todos los botones de una fila con el mismo tamaño
const sizes = async sel => page.$$eval(sel, els => els.map(e => Math.round(e.getBoundingClientRect().width) + 'x' + Math.round(e.getBoundingClientRect().height)));
for (const sel of ['.btns3 button', '#bars button', '#curve button']) { const s = await sizes(sel); ok(new Set(s).size === 1, 'mismo tamaño en ' + sel + ' (' + s.join(', ') + ')'); }

// LENGTH y ROLL mandan el parámetro con su gesto
await clear();
await page.click('#bars button:nth-child(4)');
let s = await sent();
ok(s.some(([e, o]) => e === 'moSet' && o.id === 'bars' && o.v === 3), 'LENGTH 8 manda bars=3');
ok(s.some(([e, o]) => e === 'moGesture' && o.id === 'bars' && o.begin), 'LENGTH abre el gesto (automatización)');
await clear();
await page.click('#curve button:nth-child(3)');
ok((await sent()).some(([e, o]) => e === 'moSet' && o.id === 'curve' && o.v === 2), 'ROLL LATE manda curve=2');

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

// botones del trozo
await clear();
await page.click('#cap'); await page.click('#prev'); await page.click('#load'); await page.click('#clear');
s = (await sent()).map(([e]) => e);
ok(['moCapture', 'moPreview', 'moBrowse', 'moClear'].every(e => s.includes(e)), 'CAPTURE, PREVIEW, LOAD y CLEAR avisan al plugin (' + s.join(',') + ')');
await fire('moState', { cap: 1, prev: 2, pos: 0.5 });
ok((await page.textContent('#cap')) === 'WAITING', 'CAPTURE enseña WAITING mientras espera el tiempo');
ok((await page.textContent('#prev span')) === 'STOP', 'PREVIEW pasa a STOP mientras suena');
await fire('moState', { cap: 2, prev: 1 });
ok((await page.textContent('#cap')) === 'RECORDING', 'CAPTURE enseña RECORDING');
ok((await page.textContent('#prev span')) === 'NEXT BAR', 'PREVIEW espera al siguiente compás');
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
ok((await page.textContent('#ver')) === '0.1.0', 'Settings enseña la versión');
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
await page.setViewportSize({ width: 800, height: 432 });
await page.waitForTimeout(150);
await page.screenshot({ path: out + '-small.png' });

await browser.close();
console.log(fails ? `\n${fails} FALLOS` : '\nTodo bien');
process.exit(fails ? 1 : 0);
