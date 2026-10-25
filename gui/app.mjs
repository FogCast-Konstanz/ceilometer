import { SerialClient, DeviceError, Cancelled, integer, fields, telemetry, settingCommand, capturePage, csv } from './protocol.mjs';
import { Scope, colors } from './plot.mjs';
import { DemoPort } from './demo.mjs';
const $ = id => document.getElementById(id);
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
const node = (tag, text, cls) => { const n = document.createElement(tag); if (text !== undefined) n.textContent = text; if (cls) n.className = cls; return n; };
const values = new Map(), parameters = new Map(), history = new Map();
let client = null, demo = null, sessionDemo = false, info = null, limits = null, state = null, thermal = null, bias = null, storage = null;
let telemetryNames = [], selected = ['laser_temp_mc', 'receiver_temp_mc'], samples = [], logs = [], capture = null;
let epoch = 0, busy = false, polling = false, tab = 'overview', cursor = 0, lastStorage = 0, lastLogDraw = 0;
let staged = new Map(), parameterInputs = new Map(), telemetryCells = new Map();
const builds = { laser: false, tec: false, bias: false };
const critical = ['laser_temp_mc', 'receiver_temp_mc', 'lt_output_uv', 'bias_target_uv', 'ld_error', 'tec_swen',
  'bias_enabled', 'vbus_present', 'laser_adc_raw', 'bias_adc_raw', 'dac0_readback', 'dac1_readback', 'dac2_readback', 'lt_register0'];
function notify(text, error = false) { $('notice').textContent = text; $('notice').classList.toggle('error', error); }
function trace(direction, text) {
  logs.push(`${new Date().toISOString()} ${direction} ${text}`);
  if (logs.length > 500) logs.splice(0, logs.length - 500);
}
function check(token) { if (token !== epoch || !client?.connected) throw new Cancelled('Operation cancelled.'); }
async function cmd(text, token = epoch, priority = 0) { check(token); const r = await client.command(text, priority); check(token); return r; }
const connected = () => !!client?.connected && !!info;
const idle = () => connected() && state && state[0] !== 'ACQUIRING' && !+state[5] && !+state[6] && !+thermal?.[0];
function setUi() {
  const on = connected();
  $('connect').disabled = !!client || busy || !navigator.serial || !window.isSecureContext;
  $('demo').disabled = !!client || busy; $('disconnect').disabled = !on;
  $('stop').disabled = !on;
  $('connection').textContent = on ? demo ? 'Demo connection' : 'USB connected' : client ? 'Connecting…' : 'Disconnected';
  $('connection').className = `badge${on ? ' good' : ''}`;
  for (const b of document.querySelectorAll('[data-action]')) {
    b.disabled = !on || busy || (!!b.dataset.build && (!builds[b.dataset.build] || state?.[0] === 'FAULT'));
    if (['applyParameters', 'save', 'load', 'defaults', 'revert'].includes(b.dataset.action)) b.disabled ||= !idle();
  }
  $('sendCommand').disabled = !on || busy;
  $('addChannel').disabled = !telemetryNames.length;
  $('exportTelemetry').disabled = !samples.length;
  $('exportCapture').disabled = !capture;
  $('exportParameters').disabled = !parameters.size; $('report').disabled = !info;
  for (const input of parameterInputs.values()) input.disabled = !on || busy || !idle() || input.dataset.retired === 'true';
  $('parameterStatus').textContent = on && !idle() ? 'Stop outputs before editing configuration' : 'Changes apply to RAM; Save commits flash';
}
function lost(error) {
  epoch++; busy = false; info && trace('LINK', error.message);
  state = null; thermal = bias = null;
  notify(`USB connection lost: ${error.message}. Outputs require a new connection and explicit restart.`, true);
  const old = client; client = null; demo = null;
  old?.close().catch(() => {}); $('demoBanner').hidden = true;
  $('state').textContent = 'OFFLINE'; $('state').className = 'badge';
  render();
}
async function run(label, work) {
  if (!connected() || busy) return;
  const token = ++epoch; busy = true; setUi(); notify(`${label}…`);
  try { await work(token); check(token); await refreshStatus(token); notify(`${label} complete.`); }
  catch (e) { if (!(e instanceof Cancelled)) { trace('ERROR', e.message); notify(e.message, true); } }
  finally { if (token === epoch) { busy = false; render(); } }
}
async function refreshStatus(token) {
  const previous = state?.[0];
  state = fields(await cmd('get_state', token, 1), 8);
  if (!['IDLE', 'READY', 'ACQUIRING', 'FAULT'].includes(state[0])) throw new Error('Unexpected device state.');
  for (let i = 1; i < 8; i++) integer(state[i], 0, 0xffffffff);
  if (state[0] === 'FAULT' && previous !== 'FAULT') notify('Firmware FAULT latched. Inspect feedback and peripherals, then clear the fault explicitly.', true);
  thermal = fields(await cmd('get_temperature_control', token, 1), 7);
  thermal.forEach(v => integer(v, -2147483648, 2147483647));
  bias = fields(await cmd('get_bias', token, 1), 10);
  bias.forEach(v => integer(v));
  $('counter').textContent = integer((await cmd('get_count', token))[0], 0, 65535).toLocaleString();
  if (performance.now() - lastStorage > 2000 || !storage) {
    storage = fields(await cmd('get_storage', token), 6); storage.forEach(v => integer(v)); lastStorage = performance.now();
  }
  renderStatus();
}
async function refreshParameters(token) {
  const count = integer((await cmd('parameter_count', token))[0], 1, 256);
  const next = new Map();
  for (let i = 0; i < count; i++) {
    const meta = fields(await cmd(`parameter_info,${i}`, token), 6);
    if (integer(meta[0]) !== i || !/^[a-z][a-z0-9_]{0,63}$/.test(meta[1]) || next.has(meta[1])) throw new Error('Invalid parameter descriptor.');
    const [name, value, unit, min, max, def] = fields(await cmd(`get_param,${meta[1]}`, token), 6);
    if (name !== meta[1] || unit !== meta[2] || unit.length > 40) throw new Error('Parameter metadata mismatch.');
    const d = { name, unit, min: integer(min), max: integer(max), default: integer(def), value: integer(value) };
    if (d.min !== integer(meta[3]) || d.max !== integer(meta[4]) || d.default !== integer(meta[5]) ||
        d.min > d.max || d.value < d.min || d.value > d.max) throw new Error('Invalid parameter bounds.');
    next.set(name, d);
    if (i % 8 === 0) notify(`Reading parameter inventory ${i + 1}/${count}…`);
  }
  parameters.clear(); next.forEach((v, k) => parameters.set(k, v));
  staged.clear(); renderParameters();
  $('frames').value = parameters.get('integration_frames')?.value ?? 16;
}
async function readTelemetry(name, token) {
  const sample = telemetry(await cmd(`get_telemetry,${name}`, token, -1));
  if (!/^\d+$/.test(String(name)) && sample.name !== name) throw new Error('Telemetry name mismatch.');
  values.set(sample.name, sample);
  if (selected.includes(sample.name)) {
    const data = history.get(sample.name) ?? [];
    const key = `${sample.sampled}:${sample.sequence}:${sample.valid}`;
    if (data.at(-1)?.key !== key) {
      data.push({ t: sample.received / 1000 - sample.age / 1e6, y: sample.valid ? converted(sample).value : NaN, key });
      while (data.length && (data.length > 2400 || data[0].t < performance.now() / 1000 - 125)) data.shift();
      history.set(sample.name, data);
    }
  }
  samples.push({ iso: new Date().toISOString(), demo: !!demo, ...sample });
  if (samples.length > 20000) samples.splice(0, samples.length - 20000);
  return sample;
}
async function updateAccepted(names, token) {
  for (const name of names) {
    const d = parameters.get(name); if (!d) continue;
    const reply = fields(await cmd(`get_param,${name}`, token), 6);
    if (reply[0] !== name) throw new Error('Parameter name mismatch.');
    d.value = integer(reply[1], d.min, d.max);
  }
  lastStorage = 0; renderParameters();
}
async function inventory(token) {
  await refreshParameters(token);
  const count = integer((await cmd('telemetry_count', token))[0], 1, 512), names = [];
  for (let i = 0; i < count; i++) {
    const s = await readTelemetry(i, token);
    if (names.includes(s.name)) throw new Error('Duplicate telemetry descriptor.'); names.push(s.name);
  }
  telemetryNames = names; selected = selected.filter(n => names.includes(n)); renderTelemetryStructure();
  $('telemetryCount').textContent = count; $('parameterCount').textContent = parameters.size;
  await refreshStatus(token);
}
async function connectPort(port, simulated = null) {
  busy = true; const token = ++epoch; demo = simulated; sessionDemo = !!simulated; info = null;
  values.clear(); history.clear(); samples = []; capture = null; storage = null; lastStorage = 0;
  $('plotMode').value = 'live'; $('captureDetail').textContent = 'No capture downloaded · timing and range require bench qualification';
  scope.frozen = false; scope.end = null; scope.bounds = null; plotKey = ''; $('pause').textContent = 'Pause plot';
  telemetryNames = []; parameters.clear(); staged.clear(); selected = ['laser_temp_mc', 'receiver_temp_mc'];
  renderParameters(); renderTelemetryStructure();
  const instance = new SerialClient(port, { trace, lost: error => { if (client === instance) lost(error); } });
  client = instance; setUi();
  try {
    await instance.open(); check(token);
    const identity = fields(await cmd('get_info', token), 9);
    if (identity[0] !== 'ceilometer' || identity[2] !== 'pico2') throw new Error('Selected port is not a Pico 2 ceilometer.');
    identity.slice(3).forEach(v => integer(v, 0));
    info = identity; builds.laser = !!integer(info[6], 0, 1); builds.bias = !!integer(info[7], 0, 1); builds.tec = !!integer(info[8], 0, 1);
    await cmd('stop', token, 100); await cmd('test_pulse,0', token, 100);
    limits = fields(await cmd('get_limits', token), 6); limits.forEach(v => integer(v, 0));
    $('laserCode').max = limits[0]; $('laserLimit').textContent = `ceiling ${limits[0]}`; $('laserCode').value = '0';
    $('demoBanner').hidden = !demo; $('demoCondition').value = 'normal';
    await inventory(token); check(token);
    notify(simulated ? 'Demo ready. All readings and captures are simulated.' : 'USB connected. Outputs are off; inventory and diagnostics are ready.');
  } catch (e) {
    if (client === instance) { client = null; demo = null; info = null; }
    await instance.close(); notify(e.message, true);
  } finally { if (token === epoch) { busy = false; render(); } }
}
async function stopAll(message = 'All outputs and diagnostic pulses stopped.') {
  if (!connected()) return;
  const token = ++epoch; busy = true; setUi(); notify('Stop requested; waiting for acknowledgement…');
  try { await client.stop(); check(token); await cmd('test_pulse,0', token, 100); await refreshStatus(token); notify(message); }
  catch (e) { if (!(e instanceof Cancelled)) notify(e.message, true); }
  finally { if (token === epoch) { busy = false; render(); } }
}
async function disconnect() {
  const old = client;
  if (!old) return;
  if (connected()) await stopAll();
  epoch++; client = null; demo = null; state = thermal = bias = null; busy = false;
  await old.close(); $('demoBanner').hidden = true; notify('Disconnected.'); render();
}
function converted(s) {
  if (s.unit === 'mC') return { value: Number(s.value) / 1000, unit: '°C' };
  if (/^uV(?:_|$)/.test(s.unit)) return { value: Number(s.value) / 1e6, unit: 'V' };
  return { value: Number(s.value), unit: s.unit };
}
const fresh = s => connected() && s?.valid && performance.now() - s.received <= 3500;
function metric(id, name, decimals = 2) {
  const s = values.get(name), v = s && converted(s);
  $(id).textContent = fresh(s) ? `${v.value.toFixed(decimals)} ${v.unit}` : '—';
}
function renderStatus() {
  $('identity').textContent = info ? `${info[2].toUpperCase()} · firmware ${info[1]} · ${Number(info[3]) / 1e6} MHz` : 'No board connected';
  $('state').textContent = connected() ? state?.[0] ?? 'READING' : 'OFFLINE';
  $('state').className = `badge${connected() ? state?.[0] === 'FAULT' ? ' bad' : ' good' : ''}`;
  $('builds').textContent = info ? `Build enables: laser ${builds.laser ? 'on' : 'off'} · TEC ${builds.tec ? 'on' : 'off'} · bias ${builds.bias ? 'on' : 'off'}` : 'Outputs —';
  $('storage').textContent = storage ? `Flash ${+storage[0] ? `generation ${storage[2]}` : 'defaults'} · ${+storage[1] ? 'RAM unsaved' : 'saved'}${+storage[4] ? ' · invalid record seen' : ''}` : 'Storage —';
  const enabled = (id, on, off) => { $(id).textContent = on ? 'ON' : off; $(id).className = `badge${on ? ' good' : ''}`; };
  enabled('tecStatus', connected() && +thermal?.[0], 'OFF'); enabled('biasStatus', connected() && +bias?.[0], 'OFF');
  $('armStatus').textContent = connected() && +state?.[5] ? 'ARMED' : 'DISARMED';
  $('thermalDetail').textContent = thermal ? `Target ${(thermal[1] / 1000).toFixed(2)} °C · feedback ${+thermal[2] ? 'valid' : 'invalid'} · command ${thermal[4]} mV${+thermal[6] ? ' · FAULT' : ''}` : 'Waiting for temperature feedback';
  const reasons = ['OK', 'temperature invalid', 'voltage ceiling', 'build / code ceiling', 'DAC failure'];
  $('biasDetail').textContent = bias ? `${reasons[+bias[2]] ?? 'unknown status'} · target ${(bias[7] / 1e6).toFixed(3)} V · ${+bias[3] ? 'realizable' : 'unavailable'}${+bias[1] ? ' · FAULT' : ''}` : 'Waiting for receiver feedback';
  $('footerMode').textContent = sessionDemo ? 'SIMULATED DATA · no hardware connected' : 'Board bring-up & diagnostics';
}
function renderTelemetryStructure() {
  const body = $('telemetryRows'); body.replaceChildren(); telemetryCells.clear(); $('channel').replaceChildren();
  for (const name of telemetryNames) {
    const s = values.get(name), row = node('tr'), cells = [];
    for (const text of [name, '—', s.unit, 'not read', '—', '—']) { const td = node('td', text); row.append(td); cells.push(td); }
    const td = node('td'), b = node('button', '+ Plot', 'mini'); b.addEventListener('click', () => addChannel(name)); td.append(b); row.append(td);
    body.append(row); telemetryCells.set(name, { row, cells });
    const option = node('option', `${name} · ${converted(s).unit}`); option.value = name; $('channel').append(option);
  }
  renderLegend();
}
function renderTelemetry() {
  const query = $('telemetryFilter').value.toLowerCase(); let valid = 0;
  for (const [name, { row, cells }] of telemetryCells) {
    const s = values.get(name), ok = fresh(s);
    row.hidden = !`${name} ${s?.unit}`.toLowerCase().includes(query);
    cells[1].textContent = s?.value ?? '—'; cells[1].className = ok ? '' : 'muted';
    cells[3].textContent = !connected() ? 'disconnected' : performance.now() - s.received > 3500 ? 'stale link' : s.valid ? 'valid at read' : 'invalid';
    cells[3].className = ok ? 'valid' : 'invalid';
    cells[4].textContent = s ? (s.age / 1000 + performance.now() - s.received).toFixed(0) : '—';
    cells[5].textContent = s?.sequence ?? '—'; if (ok) valid++;
  }
  $('validCount').textContent = `${valid} / ${telemetryNames.length} valid recent reads`;
}
function renderParameters() {
  const body = $('parameterRows'); body.replaceChildren(); parameterInputs.clear();
  for (const d of parameters.values()) {
    const row = node('tr'); row.dataset.name = d.name;
    row.append(node('td', d.name), node('td', d.value));
    const cell = node('td'), input = node('input'); input.type = 'number'; input.step = '1'; input.min = d.min; input.max = d.max;
    input.value = staged.get(d.name) ?? d.value; input.setAttribute('aria-label', d.name); input.dataset.retired = String(d.min === d.max);
    input.addEventListener('input', () => {
      if (input.value === String(d.value)) staged.delete(d.name); else staged.set(d.name, input.value);
      input.classList.toggle('changed', staged.has(d.name)); $('stagedCount').textContent = staged.size;
    });
    parameterInputs.set(d.name, input); cell.append(input); row.append(cell, node('td', d.unit), node('td', `${d.min} … ${d.max}`), node('td', d.default)); body.append(row);
  }
  $('stagedCount').textContent = staged.size; filterParameters(); setUi();
}
function filterParameters() { for (const row of $('parameterRows').children) row.hidden = !row.dataset.name.includes($('parameterFilter').value.toLowerCase()); }
function renderLegend() {
  $('legend').replaceChildren();
  selected.forEach((name, i) => { const b = node('button', `${name} ×`); b.style.color = colors[i]; b.addEventListener('click', () => {
    selected = selected.filter(n => n !== name); renderLegend(); updatePlot();
  }); $('legend').append(b); });
}
function addChannel(name) {
  if (!values.has(name) || selected.includes(name)) return;
  const unit = converted(values.get(name)).unit;
  if (selected.length && converted(values.get(selected[0])).unit !== unit) selected = [];
  if (selected.length >= 4) { notify('Remove a trace before adding a fifth channel.', true); return; }
  selected.push(name); $('plotMode').value = 'live'; renderLegend(); updatePlot();
}
const scope = new Scope($('scope'), { max: $('yMax'), mid: $('yMid'), min: $('yMin'), unit: $('plotUnit'), start: $('xStart'), end: $('xEnd') }, msg => notify(msg, true));
let plotKey = '';
function updatePlot() {
  const mode = $('plotMode').value, key = `${mode}:${selected.join(',')}:${capture?.at ?? ''}`;
  let series, unit;
  if (mode !== 'live') {
    series = capture ? [{ data: capture[mode].map((value, i) => ({ t: i, y: Number(value) })) }] : [];
    unit = mode === 'histogram' ? 'counts' : 'correlation × 1023';
  } else { series = selected.map(name => ({ data: history.get(name) ?? [] })); unit = values.has(selected[0]) ? converted(values.get(selected[0])).unit : '°C'; }
  if (key !== plotKey) { scope.setSeries(series, unit, mode !== 'live'); plotKey = key; } else scope.series = series;
  $('plotTitle').textContent = mode === 'live' ? 'Live telemetry' : mode === 'histogram' ? 'Capture histogram' : 'DC-rejected correlation';
  $('plotEmpty').hidden = series.some(s => s.data.length);
  $('legend').hidden = mode !== 'live';
  if (!scope.plot) { $('plotEmpty').hidden = false; $('plotEmpty').textContent = 'WebGL unavailable · telemetry tables and exports remain usable'; }
  $('window').disabled = mode !== 'live';
}
function render() {
  renderStatus(); setUi();
  metric('laserTemp', 'laser_temp_mc'); metric('receiverTemp', 'receiver_temp_mc'); metric('tecOutput', 'lt_output_uv', 3); metric('biasTarget', 'bias_target_uv', 3);
  if (tab === 'telemetry') renderTelemetry();
  $('health').replaceChildren();
  for (const [name, signal] of [['Laser DS18B20', 'laser_temp_mc'], ['Receiver MCP9800', 'receiver_temp_mc'], ['U2 laser DAC', 'dac0_readback'],
    ['U6 bias DAC', 'dac1_readback'], ['U16 threshold DAC', 'dac2_readback'], ['LT8722 SPI', 'lt_register0']]) {
    const s = values.get(signal), ok = fresh(s), item = node('div', undefined, 'health-item');
    item.append(node('b', name)); const dot = node('span', undefined, `dot${ok ? ' good' : s && connected() ? ' bad' : ''}`);
    item.append(dot, node('span', ok ? 'valid recent read' : 'invalid / not sampled')); $('health').append(item);
  }
  updatePlot();
  if (tab === 'console' && performance.now() - lastLogDraw > 250) { $('log').textContent = logs.join('\n'); $('log').scrollTop = $('log').scrollHeight; lastLogDraw = performance.now(); }
}
function download(name, content, type = 'text/plain') {
  const url = URL.createObjectURL(new Blob([content], { type })), a = node('a'); a.href = url; a.download = name; a.click(); setTimeout(() => URL.revokeObjectURL(url), 1000);
}
async function readCapture(token) {
  const snapshot = fields(await cmd('get_state', token), 8);
  if (snapshot[0] !== 'READY') throw new Error('Board result is not READY. Run a capture first.');
  const result = { at: new Date().toISOString(), demo: !!demo, frames: snapshot[1],
    chipHz: parameters.get('chip_hz')?.value ?? integer(info[4]), offset: parameters.get('range_offset_bins')?.value ?? 0 };
  for (const kind of ['histogram', 'profile']) {
    result[kind] = [];
    for (let i = 0; i < 1023; i += 16) {
      const n = Math.min(16, 1023 - i), page = await cmd(`${kind},${i},${n}`, token);
      result[kind].push(...capturePage(kind, page, i, n));
    }
  }
  check(token); capture = result; $('plotMode').value = 'histogram';
  $('captureDetail').textContent = `${demo ? 'SIMULATED · ' : ''}${result.frames} frames · ${result.chipHz.toLocaleString()} chips/s · downloaded ${result.at} · range timing unqualified`;
}
const actions = {
  pulse: t => cmd(`test_pulse,${integer($('pulse').value, 0, 10000000)}`, t), pulseOff: t => cmd('test_pulse,0', t),
  clearFault: t => cmd('clear_fault', t), refresh: inventory,
  tecOn: t => cmd('tec_enable,1', t), tecOff: t => cmd('tec_enable,0', t),
  biasOn: t => cmd('bias_enable,1', t), biasOff: t => cmd('bias_enable,0', t),
  arm: t => cmd('arm,1', t), disarm: t => cmd('arm,0', t),
  laserDac: async t => { await cmd(`laser_dac,${integer($('laserCode').value, 0, integer(limits[0]))}`, t); await updateAccepted(['laser_dac_code'], t); },
  readCapture,
  applyParameters: async t => { const command = settingCommand([...staged], parameters); await cmd(command, t); await refreshParameters(t); lastStorage = 0; },
  save: async t => { await cmd('save', t); lastStorage = 0; },
  load: async t => { await cmd('load', t); await refreshParameters(t); lastStorage = 0; },
  defaults: async t => { await cmd('defaults', t); await refreshParameters(t); lastStorage = 0; },
  revert: async () => { staged.clear(); renderParameters(); }
};
for (const [action, name] of [['capture', 'capture'], ['emit', 'start']]) actions[action] = async token => {
  await cmd(`${name},${integer($('frames').value, 1, 1000000)}`, token);
  await updateAccepted(['integration_frames'], token);
  const deadline = performance.now() + 180000;
  while (performance.now() < deadline) {
    await refreshStatus(token); render();
    if (state[0] === 'FAULT') throw new DeviceError('Capture stopped by a firmware fault. Inspect feedback before clearing.');
    if (state[0] === 'READY') { await readCapture(token); return; }
    await delay(100); check(token);
  }
  await cmd('stop', token, 100); throw new Error('Capture timed out; outputs stopped.');
};
document.querySelectorAll('[data-action]').forEach(b => b.addEventListener('click', () => run(b.textContent.trim(), actions[b.dataset.action])));
$('connect').addEventListener('click', async () => {
  if (busy || client) return;
  busy = true; setUi();
  try { const port = await navigator.serial.requestPort(); await connectPort(port); }
  catch (e) { notify(e.message, true); }
  finally { if (!client) { busy = false; setUi(); } }
});
$('demo').addEventListener('click', async () => { try {
  if (busy || client) return;
  busy = true; setUi();
  const response = await fetch('./demo-metadata.json'); if (!response.ok) throw new Error('Demo metadata unavailable.');
  const port = new DemoPort(await response.json()); await connectPort(port, port);
} catch (e) { notify(e.message, true); } finally { if (!client) { busy = false; setUi(); } } });
$('demoCondition').addEventListener('change', () => { if (demo) demo.condition = $('demoCondition').value; });
$('disconnect').addEventListener('click', () => disconnect().catch(e => notify(e.message, true)));
$('stop').addEventListener('click', () => stopAll());
$('addChannel').addEventListener('click', () => addChannel($('channel').value));
$('window').addEventListener('change', () => { scope.window = +$('window').value; scope.bounds = null; });
$('pause').addEventListener('click', () => { scope.frozen = !scope.frozen; $('pause').textContent = scope.frozen ? 'Go live' : 'Pause plot'; });
$('clearPlot').addEventListener('click', () => {
  if ($('plotMode').value === 'live') { history.clear(); samples = []; }
  else { capture = null; $('captureDetail').textContent = 'Downloaded capture cleared. Board data can be read again while READY.'; }
  scope.bounds = null; render();
});
$('plotMode').addEventListener('change', () => { scope.frozen = false; $('pause').textContent = 'Pause plot'; updatePlot(); });
$('telemetryFilter').addEventListener('input', renderTelemetry); $('parameterFilter').addEventListener('input', filterParameters);
document.querySelectorAll('[data-tab]').forEach(b => b.addEventListener('click', () => {
  tab = b.dataset.tab;
  document.querySelectorAll('[data-tab]').forEach(n => n.classList.toggle('active', n === b));
  document.querySelectorAll('.view').forEach(n => n.classList.toggle('active', n.id === tab)); render();
}));
$('commandForm').addEventListener('submit', e => { e.preventDefault(); const text = $('command').value.replace(/^\$/, '');
  if (text === 'stop') { void stopAll(); return; }
  void run('Command', async token => {
    const reply = await cmd(text, token); notify(reply.join(',') || 'ACK');
    if (/^(set_|load$|defaults$|tec_target|tec_pid|tec_polarity|laser_dac|capture|start)/.test(text)) await refreshParameters(token);
    lastStorage = 0;
  });
});
$('clearLog').addEventListener('click', () => { logs = []; $('log').textContent = ''; });
$('exportLog').addEventListener('click', () => download('ceilometer-command-log.txt', logs.join('\n')));
$('exportParameters').addEventListener('click', () => download('ceilometer-parameters.json', JSON.stringify({ demo: sessionDemo, firmware: info, parameters: Object.fromEntries([...parameters].map(([n, p]) => [n, p.value])) }, null, 2), 'application/json'));
$('report').addEventListener('click', () => download('ceilometer-diagnostics.json', JSON.stringify({ created: new Date().toISOString(), demo: sessionDemo,
  connected: connected(), info, limits, state, thermal, bias, storage, parameters: Object.fromEntries(parameters),
  telemetry: [...values.values()].map(s => ({ ...s, validAtLastRead: s.valid, linkFresh: fresh(s), browserAgeMs: performance.now() - s.received })),
  commandLog: logs }, null, 2), 'application/json'));
$('exportTelemetry').addEventListener('click', () => download('ceilometer-telemetry.csv', csv([
  ['host_utc', 'simulated', 'signal', 'raw_value_exact', 'unit', 'valid_at_read', 'device_sampled_us', 'device_age_us', 'sequence'],
  ...samples.map(s => [s.iso, s.demo, s.name, s.value, s.unit, +s.valid, s.sampled, s.age, s.sequence])]), 'text/csv'));
$('exportCapture').addEventListener('click', () => { if (!capture) return; download('ceilometer-capture.csv', csv([
  ['bin', 'uncalibrated_range_m', 'counts_exact', 'correlation_scaled_1023_exact', 'frames', 'chip_hz', 'simulated', 'downloaded_utc'],
  ...capture.histogram.map((v, i) => [i, (i - capture.offset) * 299792458 / (2 * capture.chipHz), v, capture.profile[i], capture.frames, capture.chipHz, capture.demo, capture.at])]), 'text/csv'); });
document.addEventListener('visibilitychange', () => { if (document.hidden && connected()) void stopAll('Tab hidden: outputs stopped. Restart explicitly when ready.'); });
window.addEventListener('pagehide', () => { if (connected()) { client.stop().catch(() => {}); } });
navigator.serial?.addEventListener('disconnect', e => { if (client?.port === (e.port ?? e.target)) client.fail(new Error('USB device removed.')); });
$('unsupported').hidden = !!navigator.serial && window.isSecureContext;
async function poll() {
  if (polling || busy || !connected()) return;
  polling = true; const token = epoch, begin = performance.now();
  try {
    await refreshStatus(token);
    const names = new Set([...critical, ...selected]);
    for (let i = 0; i < 12 && telemetryNames.length; i++) names.add(telemetryNames[cursor++ % telemetryNames.length]);
    for (const name of names) if (telemetryNames.includes(name)) await readTelemetry(name, token);
    $('pollStatus').textContent = `Polling · ${(performance.now() - begin).toFixed(0)} ms sweep`;
  } catch (e) {
    if (!(e instanceof Cancelled)) {
      if (e instanceof DeviceError) notify(e.message, true); else if (client) client.fail(e);
    }
  } finally { polling = false; render(); }
}
setInterval(() => { void poll(); render(); }, 150);
render();
