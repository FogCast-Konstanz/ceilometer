// Renderer adapted from crossing/drehrichtungssensor-firmware/rds-gui.
// Same vendored WebGL-Plot 1.1.2, preallocated Y buffers and 20 FPS ceiling.
export const colors = ['#61d9c7', '#a5a0ff', '#f4bc6b', '#f28da7'];
export class Scope {
  constructor(canvas, labels, onError) {
    this.canvas = canvas; this.labels = labels; this.series = []; this.window = 30;
    this.end = null; this.frozen = false; this.bounds = null; this.last = 0;
    try {
      this.gl = webglplot.setupCanvasAndWebGL(canvas, { backgroundColor: [0.035, 0.065, 0.09, 1], antialias: false });
      this.plot = new webglplot.WebglLinePlot(this.gl, 4);
      this.buffers = Array.from({ length: 4 }, () => new Float32Array(1200));
      this.plot.initLines(colors.map(color => {
        const points = new Float32Array(2400);
        for (let i = 0; i < 1200; i++) points[i * 2] = i * 2 / 1199 - 1;
        return { points, color: color.match(/\w\w/g).map(x => parseInt(x, 16) / 255).concat(1), thickness: 1, enabled: false };
      }));
      this.observer = new ResizeObserver(() => { if (canvas.clientWidth) webglplot.handleCanvasResize(canvas, this.gl); });
      this.observer.observe(canvas);
      const loop = t => { requestAnimationFrame(loop); if (t - this.last >= 50) { this.last = t; this.draw(); } };
      requestAnimationFrame(loop);
    } catch (e) { onError(`Plot unavailable: ${e.message}. Telemetry and exports remain available.`); }
  }
  setSeries(series, unit, capture = false) {
    this.series = series; this.unit = unit; this.capture = capture; this.bounds = null;
  }
  draw() {
    if (!this.plot) return;
    webglplot.clearCanvas(this.gl);
    let latest = this.capture ? Math.max(1, ...this.series.map(s => s.data.at(-1)?.t ?? 0)) : performance.now() / 1000;
    if (this.frozen && this.end === null) this.end = latest;
    if (!this.frozen) this.end = null;
    const end = this.end ?? latest, start = this.capture ? 0 : end - this.window;
    let lo = Infinity, hi = -Infinity;
    for (let c = 0; c < 4; c++) {
      const series = this.series[c]; this.plot.setLineEnabled(c, !!series);
      const b = this.buffers[c]; b.fill(NaN);
      if (!series?.data.length) continue;
      const data = series.data; let k = 0;
      while (k < data.length && data[k].t < start) k++;
      for (let i = 0; i < b.length; i++) {
        const x = start + (end - start) * i / (b.length - 1);
        while (k < data.length && data[k].t < x) k++;
        let p = data[k];
        if (!p || p.t > end || p.t - x > (this.capture ? (end / 300) : 1)) continue;
        b[i] = p.y;
        if (Number.isFinite(p.y)) { lo = Math.min(lo, p.y); hi = Math.max(hi, p.y); }
      }
    }
    if (!Number.isFinite(lo)) { lo = -1; hi = 1; }
    if (lo === hi) { lo -= 1; hi += 1; }
    const pad = (hi - lo) * .12; lo -= pad; hi += pad;
    if (!this.bounds) this.bounds = [lo, hi];
    else { this.bounds[0] = Math.min(lo, this.bounds[0] + (lo - this.bounds[0]) * .05);
      this.bounds[1] = Math.max(hi, this.bounds[1] + (hi - this.bounds[1]) * .05); }
    const [min, max] = this.bounds, mid = (max + min) / 2;
    this.buffers.forEach((b, c) => {
      for (let i = 0; i < b.length; i++) b[i] = (b[i] - mid) * 2 / (max - min);
      this.plot.updateLineY(c, b);
    });
    this.labels.max.textContent = max.toPrecision(4); this.labels.mid.textContent = mid.toPrecision(4);
    this.labels.min.textContent = min.toPrecision(4); this.labels.unit.textContent = this.unit || 'Select channels';
    this.labels.start.textContent = this.capture ? '0 bins' : `−${this.window} s`;
    this.labels.end.textContent = this.capture ? `${end} bins` : this.frozen ? 'paused' : 'now';
    this.plot.draw();
  }
}
