import { DeviceError, integer } from './protocol.mjs';
// Explicit simulator. Metadata is generated from the C descriptor lists at development time.
export class DemoPort {
  constructor(metadata) {
    this.metadata = metadata; this.params = new Map(metadata.parameters.map(p => [p.name, p.default]));
    this.saved = null; this.armed = this.tec = this.bias = this.fault = false;
    this.laser = 0; this.pulse = 0; this.frames = 0; this.ready = 0; this.condition = 'normal'; this.started = performance.now();
  }
  async open() {
    this.readable = new ReadableStream({ start: controller => { this.rx = controller; } });
    this.writable = new WritableStream({ write: data => {
      const text = new TextDecoder().decode(data).trim().replace(/^\$/, '');
      const [name, ...args] = text.split(',');
      let reply;
      try { reply = `$ACK,${name},${this.handle(name, args).join(',')}`.replace(/,$/, ''); }
      catch (e) { reply = `$NACK,${name},${e.message}`; }
      this.rx.enqueue(new TextEncoder().encode(`${reply}\r\n`));
    } });
  }
  async close() { this.readable = this.writable = null; }
  getInfo() { return {}; }
  stop() { this.armed = this.tec = this.bias = false; this.laser = 0; this.ready = 0; }
  biasModel() {
    const t = (performance.now() - this.started) / 1000, p = n => this.params.get(n);
    const temperature = 25000 + Math.round(500 * Math.sin(t / 13)) + p('receiver_temp_offset_mc');
    const breakdown = Math.round(p('sipm_breakdown_mv') * 1000 + p('sipm_tempco_uv_c') * (temperature - p('sipm_reference_mc')) / 1000);
    const target = breakdown + p('sipm_overvoltage_mv') * 1000;
    const span = p('bias_dac_ref_uv') * 6140 / 254 * p('bias_gain_ppm') / 1000000;
    const code = Math.floor((target - p('bias_offset_uv')) * 65536 / span);
    const reason = this.condition === 'receiver' ? 1 : target > Math.min(50000, p('bias_limit_mv')) * 1000 ? 2 : code > 60000 ? 3 : 0;
    return { temperature, breakdown, target, code, reason, estimate: Math.floor(span * code / 65536 + p('bias_offset_uv')) };
  }
  sample(name, unit) {
    const t = (performance.now() - this.started) / 1000;
    const laserTemp = 24000 + Math.round(120 * Math.sin(t / 8)) + this.params.get('laser_temp_offset_mc');
    const model = this.biasModel(), rxTemp = model.temperature;
    let value = 0, valid = true;
    if (name.includes('temp') || name === 'bias_feedback_mc') value = name.includes('receiver') || name.startsWith('bias') ? rxTemp : laserTemp;
    if (name.endsWith('_raw') || name.includes('_adc_raw')) value = Math.round(1000 + 150 * Math.sin(t + name.length));
    if (name === 'laser_temp_sequence' || name === 'bias_feedback_sequence') value = Math.floor(t);
    if (name === 'bias_breakdown_uv') value = model.breakdown;
    if (name === 'bias_target_uv') value = model.target;
    if (name === 'bias_estimated_uv') value = model.estimate;
    if (name === 'bias_overvoltage_uv') value = model.estimate - model.breakdown;
    if (name === 'bias_target_code') value = model.code;
    if (name === 'bias_reason') value = model.reason;
    if (name === 'bias_fault') value = +this.fault;
    if (name === 'bias_enabled') value = Number(this.bias);
    if (name === 'tec_swen') value = Number(this.tec);
    if (name === 'ld_error') value = Number(this.fault);
    if (name === 'vbus_present') value = 1;
    if (name === 'vsys_uv') value = 5000000 + Math.round(4000 * Math.sin(t));
    if (name === 'lt_output_uv') { value = this.tec ? Math.round(100000 * Math.sin(t / 4)) : 0; valid = this.tec; }
    if (name.startsWith('lt_mux') || name.startsWith('lt_ref') || name === 'lt_output_ua') valid = this.tec;
    if (name.includes('current')) valid = false; // Not calibrated, as at board bring-up.
    if (this.condition === 'receiver' && (name.startsWith('receiver') || name.startsWith('bias_'))) valid = false;
    if (this.condition === 'laser' && name.startsWith('laser_temp')) valid = false;
    return [name, unit, Math.round(value), Number(valid), (Math.floor(t * 1e6) >>> 0), 10000, Math.floor(t * 5) >>> 0].map(String);
  }
  handle(name, a) {
    const now = performance.now();
    if ((this.condition === 'receiver' && this.bias) || (this.condition === 'laser' && (this.tec || this.armed))) {
      this.stop(); this.fault = true;
    }
    const out = x => x.map(String);
    switch (name) {
      case 'get_info': return out(['ceilometer', '0.4.0-DEMO', 'pico2', 150000000, this.params.get('chip_hz'), 1023, 1, 1, 1]);
      case 'stop': this.stop(); return [];
      case 'heartbeat': return [];
      case 'get_limits': return out([1, 60000, 500, 150000000, 1023, 4186112]);
      case 'get_state': return out([this.fault ? 'FAULT' : this.ready > now ? 'ACQUIRING' : this.ready ? 'READY' : 'IDLE',
        this.ready && this.ready <= now ? this.frames : 0, this.ready > now ? this.frames : 0, 1023, 0, +this.armed, +this.bias, 250]);
      case 'get_temperature_control': return out([+this.tec, this.params.get('tec_target_mc'), +(this.condition !== 'laser'), 24000, this.tec ? 100 : 0, this.params.get('tec_polarity'), +(this.fault && this.condition === 'laser')]);
      case 'get_bias': {
        const b = this.biasModel();
        return out([+this.bias, +this.fault, b.reason, +!b.reason, b.temperature, 600000, b.breakdown, b.target, b.estimate, b.code]);
      }
      case 'get_storage': return out([+!!this.saved, +(!this.saved || JSON.stringify([...this.params]) !== JSON.stringify(this.saved)), this.saved ? 1 : 0, this.saved ? 0 : -1, 0, 2]);
      case 'parameter_count': return out([this.metadata.parameters.length, 2]);
      case 'parameter_info': {
        const p = this.metadata.parameters[integer(a[0], 0, this.metadata.parameters.length - 1)];
        return out([a[0], p.name, p.unit, p.min, p.max, p.default]);
      }
      case 'get_param': {
        const p = this.metadata.parameters.find(p => p.name === a[0]); if (!p) throw new DeviceError('UNKNOWN_PARAMETER');
        return out([p.name, this.params.get(p.name), p.unit, p.min, p.max, p.default]);
      }
      case 'telemetry_count': return out([this.metadata.telemetry.length]);
      case 'get_telemetry': {
        const p = /^\d+$/.test(a[0]) ? this.metadata.telemetry[+a[0]] : this.metadata.telemetry.find(p => p.name === a[0]);
        if (!p) throw new DeviceError('UNKNOWN_TELEMETRY'); return this.sample(p.name, p.unit);
      }
      case 'clear_fault': if (this.condition !== 'normal') throw new DeviceError('TEMPERATURE_INVALID'); this.fault = false; return [];
      case 'tec_enable':
        if (a[0] === '1' && (this.fault || this.condition === 'laser' || !this.params.get('tec_polarity'))) throw new DeviceError('TEC_DISABLED_TEMPERATURE_OR_POLARITY');
        this.tec = a[0] === '1'; return [];
      case 'bias_enable':
        if (a[0] === '1' && (this.fault || this.biasModel().reason)) throw new DeviceError('BIAS_FEEDBACK_OR_LIMIT');
        this.bias = a[0] === '1'; return [];
      case 'arm': if (a[0] === '0') this.stop(); else { if (this.fault || this.condition === 'laser') throw new DeviceError('TEMPERATURE_INVALID'); this.armed = true; } return [];
      case 'laser_dac': if (+a[0] > 1 || (+a[0] && !this.armed)) throw new DeviceError('NOT_ARMED_OR_LIMIT'); this.laser = +a[0]; this.params.set('laser_dac_code', this.laser); return [];
      case 'test_pulse': this.pulse = integer(a[0], 0, 10000000); return out([this.pulse]);
      case 'get_test_pulse': return out([this.pulse]);
      case 'get_count': return out([Math.floor(now * this.pulse / 1000) % 65536]);
      case 'capture': case 'start':
        if (this.fault || (name === 'start' && (!this.armed || !this.laser))) throw new DeviceError('NOT_ARMED_OR_FAULT');
        this.frames = integer(a[0], 1, 1000000); this.params.set('integration_frames', this.frames); this.ready = now + 600; return [];
      case 'histogram': case 'profile': {
        if (!this.ready || this.ready > now) throw new DeviceError('NO_DATA');
        const offset = integer(a[0], 0, 1022), n = integer(a[1], 1, 16);
        return out([offset, ...Array.from({ length: n }, (_, i) => Math.round((name === 'histogram' ? 20 : 0) +
          (name === 'histogram' ? 70 : 7000) * Math.exp(-(((offset + i - 110) / 8) ** 2)) + 3 * Math.sin(offset + i)))]);
      }
      case 'save': case 'load': case 'defaults': case 'set_params': {
        if (this.armed || this.tec || this.bias || this.ready > now) throw new DeviceError('OUTPUTS_ACTIVE');
        if (name === 'save') { this.saved = [...this.params]; return []; }
        if (name === 'load') { if (!this.saved) throw new DeviceError('NO_VALID_SAVED_PARAMETERS'); this.params = new Map(this.saved); return []; }
        if (name === 'defaults') { this.params = new Map(this.metadata.parameters.map(p => [p.name, p.default])); return []; }
        const candidate = new Map(this.params);
        if (!a.length || a.length % 2 || a.length > 16) throw new DeviceError('INVALID_CONFIGURATION');
        for (let i = 0; i < a.length; i += 2) {
          const p = this.metadata.parameters.find(p => p.name === a[i]); if (!p) throw new DeviceError('UNKNOWN_PARAMETER');
          candidate.set(p.name, integer(a[i + 1], p.min, p.max));
        }
        const v = n => candidate.get(n);
        if (v('laser_min_mc') >= v('laser_max_mc') || v('tec_min_mc') > v('laser_min_mc') || v('tec_max_mc') < v('laser_max_mc') ||
            v('tec_target_mc') < v('laser_min_mc') || v('tec_target_mc') > v('laser_max_mc') ||
            v('temperature_age_ms') < v('ds_period_ms') + 850 || v('pid_integral_mv') > v('tec_output_mv') ||
            150000000 % v('chip_hz') || (150000000 / v('chip_hz')) % 15) throw new DeviceError('INVALID_CONFIGURATION');
        this.params = candidate; return [];
      }
      case 'tec_read': return out([integer(a[0], 0, 7), 0]);
      default: throw new DeviceError('UNKNOWN_COMMAND_OR_INVALID_PARAM');
    }
  }
}
