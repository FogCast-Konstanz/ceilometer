import { Script } from 'node:vm';
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { SerialClient, DeviceError, Cancelled, requestText, integer, telemetry, settingCommand, capturePage, csv } from '../protocol.mjs';
import { DemoPort } from '../demo.mjs';
const tick = () => new Promise(r => setTimeout(r, 0));
class Port {
  writes = []; closed = false;
  async open() {
    this.readable = new ReadableStream({ start: c => { this.rx = c; } });
    this.writable = new WritableStream({ write: b => { this.writes.push(new TextDecoder().decode(b)); } });
  }
  reply(s) { this.rx.enqueue(new TextEncoder().encode(s)); }
  async close() { this.closed = true; }
}
test('ASCII framing, fixed integer ranges, atomic settings and spreadsheet-safe exact CSV', () => {
  assert.equal(requestText('$get_info'), 'get_info');
  for (const s of ['stop\narm,1', 'stop\r', 'get_info,é', 'x'.repeat(127), '$$stop']) assert.throws(() => requestText(s));
  assert.equal(integer('-2147483648'), -2147483648);
  for (const s of ['1.5', '1x', '', '9007199254740993']) assert.throws(() => integer(s));
  const d = new Map([['tec_target_mc', { min: 10000, max: 30000 }]]);
  assert.equal(settingCommand([['tec_target_mc', '25000']], d), 'set_params,tec_target_mc,25000');
  assert.throws(() => settingCommand([['tec_target_mc', '30001']], d));
  assert.throws(() => settingCommand([], d));
  assert.throws(() => settingCommand(Array(9).fill(['tec_target_mc', '25000']), d));
  assert.match(csv([['9223372036854775807', '-42', '=HYPERLINK("evil")']]), /"9223372036854775807","-42","'=HYPERLINK\(""evil""\)"/);
});
test('int64 telemetry stays exact and malformed validity/age/shape are rejected', () => {
  const r = ['counter_raw', 'code', '9223372036854775807', '1', '4294967295', '12', '4'];
  const s = telemetry(r); assert.equal(s.value, r[2]); assert.equal(s.valid, true);
  for (const [i, value] of [[2, '9223372036854775808'], [3, '2'], [4, '-1'], [5, '4294967296'], [0, '=evil']]) {
    const bad = [...r]; bad[i] = value; assert.throws(() => telemetry(bad));
  }
  assert.throws(() => telemetry(r.slice(1)));
});
test('acquisition pages validate offsets, completeness and signed/unsigned 64-bit limits', () => {
  assert.deepEqual(capturePage('histogram', ['0', '18446744073709551615'], 0, 1), ['18446744073709551615']);
  assert.deepEqual(capturePage('profile', ['1022', '-9223372036854775808'], 1022, 1), ['-9223372036854775808']);
  assert.throws(() => capturePage('histogram', ['0', '-1'], 0, 1));
  assert.throws(() => capturePage('histogram', ['0', '18446744073709551616'], 0, 1));
  assert.throws(() => capturePage('profile', ['0', '9223372036854775808'], 0, 1));
  assert.throws(() => capturePage('profile', ['1', '0'], 0, 1));
  assert.throws(() => capturePage('profile', ['0'], 0, 1));
  assert.throws(() => capturePage('profile', ['1022', '0', '0'], 1022, 2));
});
test('offline demo descriptors match actual firmware parameter and telemetry inventories', async () => {
  const meta = JSON.parse(await readFile(new URL('../demo-metadata.json', import.meta.url)));
  const ph = await readFile(new URL('../../../firmware/pico2/include/parameters.h', import.meta.url), 'utf8');
  const th = await readFile(new URL('../../../firmware/pico2/include/telemetry.h', import.meta.url), 'utf8');
  const parameters = [...ph.matchAll(/X\((\w+),"([^"]+)",(-?\d+),(-?\d+),(-?\d+)\)/g)]
    .map(([, name, unit, min, max, def]) => ({ name, unit, min: +min, max: +max, default: +def }));
  const telemetry = [...th.matchAll(/X\((\w+),"([^"]+)"\)/g)].map(([, name, unit]) => ({ name, unit }));
  assert.deepEqual(meta, { parameters, telemetry });
  const piMeta = JSON.parse(await readFile(new URL('../../instrument/src/fogcast/demo-metadata.json', import.meta.url)));
  assert.deepEqual(piMeta, meta);
});
test('serialized requests accept fragmented CRLF and preserve uint64 array text', async () => {
  const port = new Port(), client = new SerialClient(port); await client.open();
  const first = client.command('get_info'), second = client.command('histogram,0,1');
  await tick(); assert.deepEqual(port.writes, ['$get_info\n']);
  port.reply('$ACK,get_'); port.reply('info,ceilometer\r\n');
  assert.deepEqual(await first, ['ceilometer']); await tick(); assert.equal(port.writes.length, 2);
  port.reply('$ACK,histogram,0,18446744073709551615\n');
  assert.deepEqual(await second, ['0', '18446744073709551615']);
  await client.close(); assert.equal(port.closed, true);
});
test('Stop removes queued actuation and is next after the in-flight ACK', async () => {
  const port = new Port(), client = new SerialClient(port); await client.open();
  const pending = client.command('get_state');
  const cancelled = assert.rejects(client.command('arm,1'), Cancelled);
  const stop = client.stop(); await tick();
  assert.equal(port.writes.length, 1); port.reply('$ACK,get_state,IDLE\n'); await pending;
  await tick(); assert.deepEqual(port.writes, ['$get_state\n', '$stop\n']);
  port.reply('$ACK,stop\n'); await stop; await cancelled; await client.close();
});
test('NACK rejects one request without breaking the synchronized session', async () => {
  const port = new Port(), client = new SerialClient(port); await client.open();
  const failed = assert.rejects(client.command('arm,1'), DeviceError);
  await tick(); port.reply('$NACK,arm,LASER_BUILD_DISABLED\r\n'); await failed;
  assert.equal(client.connected, true);
  const next = client.command('heartbeat'); await tick(); port.reply('$ACK,heartbeat\n'); await next; await client.close();
});
test('timeout closes the port and rejects queued commands; no late-ACK reuse', async () => {
  const port = new Port(); let lost = 0;
  const client = new SerialClient(port, { timeout: 30, lost: () => lost++ }); await client.open();
  const a = assert.rejects(client.command('get_info'), /timed out/), b = assert.rejects(client.command('arm,1'), /timed out/);
  await Promise.all([a, b]); await client.close();
  assert.equal(lost, 1); assert.equal(client.connected, false); assert.equal(port.closed, true);
  assert.deepEqual(port.writes, ['$get_info\n']); await assert.rejects(client.command('heartbeat'), /disconnected/);
});
test('unexpected reply, invalid byte and oversized response fail closed', async () => {
  for (const response of ['$ACK,arm\n', '$ACK,get_info,\u00ff\n', 'x'.repeat(8193)]) {
    const port = new Port(), client = new SerialClient(port); await client.open();
    const result = assert.rejects(client.command('get_info')); await tick(); port.reply(response);
    await result; await client.close(); assert.equal(port.closed, true);
  }
});
test('demo runs through the same Web Streams transport and device protocol', async () => {
  const metadata = JSON.parse(await readFile(new URL('../demo-metadata.json', import.meta.url)));
  assert.equal(metadata.parameters.length, 52); assert.equal(metadata.telemetry.length, 108);
  const port = new DemoPort(metadata), client = new SerialClient(port); await client.open();
  assert.match((await client.command('get_info'))[1], /DEMO/);
  assert.equal((await client.command('parameter_count'))[0], '52');
  assert.deepEqual(await client.command('i2c_scan,1'), ['1','1','1','1','1','0','110','2','72','78']);
  await assert.rejects(client.command('tec_enable,1'), DeviceError);
  await client.command('set_params,tec_polarity,1'); await client.command('tec_enable,1');
  assert.equal((await client.command('get_temperature_control'))[0], '1');
  await assert.rejects(client.command('i2c_scan,1'), DeviceError);
  port.condition = 'laser'; assert.equal((await client.command('get_state'))[0], 'FAULT');
  assert.equal((await client.command('get_temperature_control'))[0], '0');
  assert.equal(telemetry(await client.command('get_telemetry,laser_temp_mc')).valid, false);
  await assert.rejects(client.command('clear_fault'), DeviceError);
  port.condition = 'normal'; await client.command('clear_fault'); await client.command('stop');
  await client.command('set_params,sipm_overvoltage_mv,9000'); await client.command('save');
  await client.command('defaults'); await client.command('load');
  assert.equal((await client.command('get_param,sipm_overvoltage_mv'))[1], '9000');
  await client.command('set_params,laser_max_code,100');
  assert.equal((await client.command('get_limits'))[0], '100');
  await client.command('arm,1');
  await assert.rejects(client.command('laser_dac,101'), DeviceError);
  await client.command('laser_dac,100'); await client.command('stop');
  await client.command('save'); await client.command('defaults'); await client.command('load');
  assert.equal((await client.command('get_param,laser_max_code'))[1], '100');
  await assert.rejects(client.command('set_params,laser_min_mc,30000,laser_max_mc,10000'), DeviceError);
  await client.close();
});

test('direct-file entry uses valid classic JavaScript with embedded demo metadata', async () => {
  const html = await readFile(new URL('../index.html', import.meta.url), 'utf8');
  const bundle = await readFile(new URL('../app.bundle.js', import.meta.url), 'utf8');
  assert.match(html, /<script src="app.bundle.js"><\/script>/);
  assert.doesNotMatch(html, /type="module"/);
  assert.doesNotThrow(() => new Script(bundle));
  assert.doesNotMatch(bundle, /\bfetch\s*\(/);
  assert.match(bundle, /const demoMetadata=/);
});
