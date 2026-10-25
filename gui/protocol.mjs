export class DeviceError extends Error {}
export class Cancelled extends Error {}
export function requestText(text) {
  text = text.replace(/^\$/, '');
  if (!/^[a-z_]+(?:,[\x20-\x7e]+)?$/.test(text) || text.length + 1 > 127)
    throw new Error('Use one ASCII command, at most 127 characters including $.');
  return text;
}
export function integer(text, min = Number.MIN_SAFE_INTEGER, max = Number.MAX_SAFE_INTEGER) {
  if (!/^-?\d+$/.test(String(text))) throw new Error(`Invalid integer: ${text}`);
  const n = Number(text);
  if (!Number.isSafeInteger(n) || n < min || n > max) throw new Error(`Integer outside ${min}…${max}: ${text}`);
  return n;
}
export function fields(reply, count) {
  if (reply.length !== count) throw new Error(`Expected ${count} response fields, received ${reply.length}.`);
  return reply;
}
export function telemetry(reply) {
  fields(reply, 7);
  const [name, unit, value, valid, sampled, age, sequence] = reply;
  if (!/^[a-z][a-z0-9_]{0,63}$/.test(name) || !/^-?\d+$/.test(value) || unit.length > 40)
    throw new Error('Malformed telemetry response.');
  const exact = BigInt(value);
  if (exact < -(1n << 63n) || exact >= 1n << 63n) throw new Error('Telemetry exceeds int64.');
  return { name, unit, value, valid: !!integer(valid, 0, 1), sampled: integer(sampled, 0, 0xffffffff),
    age: integer(age, 0, 0xffffffff), sequence: integer(sequence, 0, 0xffffffff), received: performance.now() };
}
export function settingCommand(changes, descriptors) {
  if (!changes.length || changes.length > 8) throw new Error('Apply between one and eight staged values atomically.');
  const pairs = [];
  for (const [name, value] of changes) {
    const d = descriptors.get(name);
    if (!d) throw new Error(`Unknown parameter ${name}`);
    pairs.push(name, String(integer(value, d.min, d.max)));
  }
  return requestText(`set_params,${pairs.join(',')}`);
}
export function csvCell(value) {
  let s = String(value ?? '');
  if (/^[=+@-]/.test(s) && !/^-?\d+(?:\.\d+)?(?:e[+-]?\d+)?$/i.test(s)) s = `'${s}`;
  return `"${s.replaceAll('"', '""')}"`;
}
export function csv(rows) { return rows.map(row => row.map(csvCell).join(',')).join('\r\n'); }
export function capturePage(kind, reply, offset, count) {
  if (!['histogram', 'profile'].includes(kind) || !Number.isInteger(offset) || !Number.isInteger(count) ||
      offset < 0 || count < 1 || count > 16 || offset + count > 1023) throw new Error('Invalid acquisition page request.');
  fields(reply, count + 1);
  if (integer(reply[0]) !== offset) throw new Error('Acquisition offset mismatch.');
  const values = reply.slice(1), min = kind === 'histogram' ? 0n : -(1n << 63n);
  const max = kind === 'histogram' ? (1n << 64n) - 1n : (1n << 63n) - 1n;
  for (const v of values) if (!/^-?\d+$/.test(v) || BigInt(v) < min || BigInt(v) > max) throw new Error('Invalid acquisition value.');
  return values;
}

// One request at a time: the firmware has no transaction IDs. A timeout or
// mismatched response poisons the session; late ACKs must never satisfy a new request.
export class SerialClient {
  constructor(port, { trace = () => {}, lost = () => {}, timeout = 800 } = {}) {
    Object.assign(this, { port, trace, lost, timeout, connected: false, queue: [], pending: null, buffer: '' });
  }
  async open() {
    await this.port.open({ baudRate: 115200, bufferSize: 65536 }); // USB CDC line coding only.
    try {
      this.writer = this.port.writable.getWriter();
      this.reader = this.port.readable.getReader();
      this.connected = true;
      this.readTask = this.readLoop();
    } catch (e) { await this.close(); throw e; }
  }
  command(text, priority = 0) {
    try { text = requestText(text); } catch (e) { return Promise.reject(e); }
    if (!this.connected) return Promise.reject(new Error('USB is disconnected.'));
    return new Promise((resolve, reject) => {
      this.queue.push({ text, priority, resolve, reject });
      this.queue.sort((a, b) => b.priority - a.priority);
      this.pump();
    });
  }
  cancelQueued() {
    for (const job of this.queue.splice(0)) job.reject(new Cancelled('Stopped by operator.'));
  }
  stop() { this.cancelQueued(); return this.command('stop', 100); }
  pump() {
    if (!this.connected || this.pending || !this.queue.length) return;
    const job = this.queue.shift(); this.pending = job;
    job.timer = setTimeout(() => this.fail(new Error(`USB response timed out: ${job.text}`)), this.timeout);
    this.trace('TX', `$${job.text}`);
    // A stalled USB write is covered by the same request deadline as the read.
    this.writer.write(new TextEncoder().encode(`$${job.text}\n`)).catch(e => this.fail(e));
  }
  receive(chunk) {
    for (const c of chunk) {
      if (c === '\r') continue;
      if (c !== '\n') {
        if (c.charCodeAt(0) < 32 || c.charCodeAt(0) > 126 || this.buffer.length >= 8192)
          throw new Error('Invalid or oversized USB response.');
        this.buffer += c; continue;
      }
      const line = this.buffer; this.buffer = '';
      if (!line) continue;
      this.trace('RX', line);
      const f = line.split(','), job = this.pending;
      if (!job || !['$ACK', '$NACK'].includes(f[0]) || f[1] !== job.text.split(',')[0])
        throw new Error(`Unexpected USB response: ${line}`);
      clearTimeout(job.timer); this.pending = null;
      if (f[0] === '$NACK') job.reject(new DeviceError(line)); else job.resolve(f.slice(2));
      // Leave the stream callback before transmitting the next request.
      queueMicrotask(() => this.pump());
    }
  }
  async readLoop() {
    const decoder = new TextDecoder('ascii', { fatal: true });
    try {
      while (this.connected) {
        const { value, done } = await this.reader.read();
        if (done) { if (this.connected) this.fail(new Error('USB receive stream closed.')); break; }
        this.receive(decoder.decode(value, { stream: true }));
      }
    } catch (e) { if (this.connected) this.fail(e); }
    finally { this.reader.releaseLock(); }
  }
  fail(error, notify = true) {
    if (!this.connected && this.closing) return;
    this.connected = false;
    if (this.pending) { clearTimeout(this.pending.timer); this.pending.reject(error); this.pending = null; }
    for (const job of this.queue.splice(0)) job.reject(error);
    this.closing = this.closePort();
    if (notify) this.lost(error);
  }
  async closePort() {
    try { await this.reader?.cancel(); } catch { /* removal already closed it */ }
    await this.readTask;
    try { this.writer?.releaseLock(); } catch { /* already released */ }
    try { await this.port.close(); } catch { /* physically removed */ }
  }
  async close() { if (!this.closing) this.fail(new Cancelled('Connection closed.'), false); await this.closing; }
}
