// BoomTown Beats Clip 96 — DSP core (browser prototype).
// Push -> [optional band split: the lows get their own pre-clip] -> oversampled clipper (shape, asym)
//      -> lookahead limiter (1.5 ms, release, stereo link) -> safety clamp at the ceiling -> Delta / Match -> output.
// Modes: Clip (clipper only), Clip + Limit (clipper 1.5 dB above the ceiling, limiter catches the rest), Limit.
// The limiter's lookahead delay is always in the path, so the latency only depends on Quality, not on the mode.
function clipModule(){
  'use strict';
  const TAU = Math.PI * 2;
  const dbl = d => Math.pow(10, d / 20);
  const clampf = (v, a, b) => v < a ? a : (v > b ? b : v);

  function setLP2(o, fs, f, q){
    f = clampf(f, 20, fs * 0.45);
    const w = TAU * f / fs, sn = Math.sin(w), cs = Math.cos(w), al = sn / (2 * q), a0 = 1 + al;
    o[0] = (1 - cs) / 2 / a0; o[1] = (1 - cs) / a0; o[2] = o[0]; o[3] = -2 * cs / a0; o[4] = (1 - al) / a0;
  }
  function bq(c, s, x){ const y = c[0] * x + s[0]; s[0] = c[1] * x - c[3] * y + s[1]; s[1] = c[2] * x - c[4] * y; return y; }

  function besselI0(x){ let s = 1, t = 1; for (let k = 1; k < 32; k++){ const r = x / (2 * k); t *= r * r; s += t; } return s; }
  class Oversampler {
    constructor(f){
      this.f = f; if (f === 1) return;
      const nReal = f === 2 ? 129 : 257, N = Math.ceil(nReal / f) * f;
      const h = new Float64Array(N), fc = 0.5 / f * 0.9, beta = 7, m = (nReal - 1) / 2, i0b = besselI0(beta);
      let sum = 0;
      for (let n = 0; n < nReal; n++){
        const t = n - m, x = 2 * fc * t, sinc = t === 0 ? 1 : Math.sin(Math.PI * x) / (Math.PI * x), r = 2 * n / (nReal - 1) - 1;
        h[n] = 2 * fc * sinc * besselI0(beta * Math.sqrt(Math.max(0, 1 - r * r))) / i0b; sum += h[n];
      }
      for (let n = 0; n < N; n++) h[n] /= sum;
      this.N = N; this.nReal = nReal; this.h = h; this.L = N / f; this.ph = [];
      for (let p = 0; p < f; p++){ const a = new Float64Array(this.L); for (let j = 0; j < this.L; j++) a[j] = h[p + j * f] * f; this.ph.push(a); }
      this.ui = new Float64Array(this.L * 2); this.up = 0; this.di = new Float64Array(N * 2); this.dp = 0;
    }
    upsample(x, out){
      const L = this.L; this.up = (this.up + 1) % L; this.ui[this.up] = x; this.ui[this.up + L] = x;
      const b = this.up + L, ui = this.ui;
      for (let p = 0; p < this.f; p++){ const a = this.ph[p]; let s = 0; for (let j = 0; j < L; j++) s += a[j] * ui[b - j]; out[p] = s; }
    }
    push(x){ const N = this.N; this.dp = (this.dp + 1) % N; this.di[this.dp] = x; this.di[this.dp + N] = x; }
    // aligned to the first sub-sample of the group, so up + down is exactly 64 base-rate samples (h is zero past nReal)
    down(){ const h = this.h, di = this.di, b = this.dp + this.N - (this.f - 1); let s = 0; for (let k = 0; k < this.nReal; k++) s += h[k] * di[b - k]; return s; }
  }

  const MODES = ['Clip', 'Clip + Limit', 'Limit'];
  const DEFAULTS = { mode:1, quality:2, push:6, shape:50, asym:0, split:40, lowDrive:0, release:60, link:100,
                     ceiling:-0.3, out:0, delta:0, match:0, bypass:0 };
  const LOG = { split:1, release:1 };
  const NOSMOOTH = { mode:1, quality:1, delta:1, match:1 };
  const BLOCK = 32, MAXD = 1024;                   // delay buffers (power of two, > latency at 192 kHz)
  const lookaheadFor = fs => Math.max(1, Math.round(0.0015 * fs));
  const latencyFor = (os, fs) => (os === 1 ? 0 : 64) + lookaheadFor(fs) - 1;
  const SPLIT_OFF = 40.5;                           // split knob at its minimum = Off

  class Engine {
    constructor(fs){
      this.fs = fs;
      this.tgt = Object.assign({}, DEFAULTS);
      this.cur = Object.assign({}, DEFAULTS);
      this.cLP = new Float64Array(5);
      this.sLP = [[new Float64Array(2), new Float64Array(2)], [new Float64Array(2), new Float64Array(2)]];
      this.La = lookaheadFor(fs);
      // limiter state
      this.req = [new Float64Array(this.La).fill(1), new Float64Array(this.La).fill(1)]; this.rp = 0;
      this.box = new Float64Array(this.La); this.boxSum = this.La; this.box.fill(1); this.bp = 0;
      this.env = 1;
      this.lim = [new Float64Array(MAXD), new Float64Array(MAXD)]; this.lw = 0;     // limiter lookahead delay
      this.raw = [new Float64Array(MAXD), new Float64Array(MAXD)]; this.ww = 0;     // dry (bypass / delta) delay
      this.ub = new Float64Array(4);
      this.inPk = 0; this.outPk = 0; this.pushPk = 0; this.redPk = 0; this.grMin = 1; this.mCount = 0;
      this.onMeter = null;
      this.setQuality(this.cur.quality);
      this.design(); this.derive();
    }
    setParams(p){ Object.assign(this.tgt, p); }
    latency(){ return latencyFor(this.os, this.fs); }
    setQuality(q){
      this.os = q === 1 || q === 4 ? q : 2;
      this.U = [new Oversampler(this.os), new Oversampler(this.os)];
      this.D = [new Oversampler(this.os), new Oversampler(this.os)];
      this.dcx = [0, 0]; this.dcy = [0, 0];
      this.dcR = 1 - TAU * 5 / (this.fs * this.os);
    }
    smooth(){
      const t = this.tgt, c = this.cur, a = 1 - Math.exp(-BLOCK / (this.fs * 0.025)); let moved = false;
      if ((t.quality | 0) !== this.os && (t.quality === 1 || t.quality === 2 || t.quality === 4)){ c.quality = t.quality; this.setQuality(t.quality); moved = true; }
      for (const k in t){
        const target = t[k], v = c[k]; if (v === target) continue;
        let nv;
        if (NOSMOOTH[k]) nv = target;
        else if (LOG[k]){ nv = v * Math.pow(target / v, a); if (Math.abs(nv / target - 1) < 1e-4) nv = target; }
        else { nv = v + (target - v) * a; if (Math.abs(nv - target) < 1e-5) nv = target; }
        c[k] = nv; moved = true;
      }
      return moved;
    }
    design(){ setLP2(this.cLP, this.fs, Math.max(this.cur.split, 40), 0.7071); }
    derive(){
      const c = this.cur;
      this.pushG = dbl(c.push);
      this.ceil = dbl(c.ceiling);
      const m = c.mode | 0;
      this.useClip = m !== 2; this.useLim = m !== 0;
      this.clipC = m === 1 ? this.ceil * dbl(1.5) : this.ceil;     // the clipper sits a little above the limiter
      this.p = 2 + 18 * c.shape / 100;
      this.ip = 1 / this.p;
      this.asA = 0.6 * c.asym / 100;
      this.asH = this.h(this.asA); this.asN = 1 / (1 + this.asH);
      this.splitAmt = clampf((c.split - SPLIT_OFF) / 10, 0, 1);
      this.lowG = dbl(c.lowDrive);
      this.rel = 1 - Math.exp(-1 / (this.fs * c.release / 1000));
      this.link = c.link / 100;
      this.outG = dbl(c.out) * (c.match > 0.5 ? 1 / this.pushG : 1);
      this.delta = c.delta > 0.5; this.byp = c.bypass;
    }
    h(v){ return v / Math.pow(1 + Math.pow(Math.abs(v), this.p), this.ip); }
    // clip curve normalised to 1; asym shifts the operating point (even harmonics) and keeps |y| < 1
    curve(v){ return this.asA === 0 ? this.h(v) : (this.h(v + this.asA) - this.asH) * this.asN; }
    clipOS(u, ch){
      const c = this.clipC; let y = c * this.curve(u / c);
      if (this.asA !== 0){ const d = y - this.dcx[ch] + this.dcR * this.dcy[ch]; this.dcx[ch] = y; this.dcy[ch] = d; y = d; }
      return y;
    }
    process(iL, iR, oL, oR, n){
      const ub = this.ub, La = this.La;
      for (let b0 = 0; b0 < n; b0 += BLOCK){
        const e = Math.min(n, b0 + BLOCK);
        if (this.smooth()) this.design();
        this.derive();
        const os = this.os, lat = this.latency();
        const limDelay = La - 1;
        for (let s = b0; s < e; s++){
          const inL = iL[s], inR = iR[s];
          const pi = Math.max(Math.abs(inL), Math.abs(inR)); if (pi > this.inPk) this.inPk = pi;
          // ---- clip stage (per channel) ----
          let cl = 0, cr = 0;
          for (let ch = 0; ch < 2; ch++){
            let u = (ch ? inR : inL) * this.pushG;
            const ap = Math.abs(u); if (ap > this.pushPk) this.pushPk = ap;
            if (this.splitAmt > 0){
              const sl = this.sLP[ch];
              const lo = bq(this.cLP, sl[1], bq(this.cLP, sl[0], u)), hi = u - lo;
              const c = this.clipC, g = this.lowG;
              const lo2 = c * this.h(lo * g / c) / g;           // lows pre-clipped on their own
              u = hi + lo + (lo2 - lo) * this.splitAmt;
            }
            let y;
            if (!this.useClip){
              y = u;                                            // keep the same latency as the oversampled path
              if (os !== 1){ const U = this.U[ch], D = this.D[ch]; U.upsample(u, ub); for (let p = 0; p < os; p++) D.push(ub[p]); y = D.down(); }
            } else if (os === 1) y = this.clipOS(u, ch);
            else { const U = this.U[ch], D = this.D[ch]; U.upsample(u, ub); for (let p = 0; p < os; p++) D.push(this.clipOS(ub[p], ch)); y = D.down(); }
            if (ch) cr = y; else cl = y;
          }
          // ---- lookahead limiter, linked ----
          const lw = this.lw;
          this.lim[0][lw] = cl; this.lim[1][lw] = cr;
          let g = 1;
          if (this.useLim){
            const c = this.ceil;
            const rl = Math.min(1, c / (Math.abs(cl) + 1e-12)), rr = Math.min(1, c / (Math.abs(cr) + 1e-12));
            const mn = Math.min(rl, rr);
            // per-channel gains are blended toward the linked gain; the louder of the two drives the hold
            const gl = mn + (rl - mn) * (1 - this.link), gr = mn + (rr - mn) * (1 - this.link);
            this.req[0][this.rp] = gl; this.req[1][this.rp] = gr;
          } else { this.req[0][this.rp] = 1; this.req[1][this.rp] = 1; }
          this.rp = (this.rp + 1) % La;
          // hold-min over the lookahead window (gains per channel collapsed to their minimum for the box filter,
          // the per-channel difference is applied as a ratio so unlinked channels can differ)
          let m0 = 1, m1 = 1; const q0 = this.req[0], q1 = this.req[1];
          for (let k = 0; k < La; k++){ if (q0[k] < m0) m0 = q0[k]; if (q1[k] < m1) m1 = q1[k]; }
          const mm = Math.min(m0, m1);
          this.boxSum += mm - this.box[this.bp]; this.box[this.bp] = mm; this.bp = (this.bp + 1) % La;
          const avg = Math.min(1, this.boxSum / La);
          this.env = avg < this.env ? avg : this.env + (avg - this.env) * this.rel;
          g = this.env;
          const ri = (lw - limDelay + MAXD) & (MAXD - 1);
          this.lw = (lw + 1) & (MAXD - 1);
          const gL = mm < 1 ? g * Math.min(1, m0 / mm) : g, gR = mm < 1 ? g * Math.min(1, m1 / mm) : g;
          if (g < this.grMin) this.grMin = g;
          let yl = this.lim[0][ri] * gL, yr = this.lim[1][ri] * gR;
          // safety clamp: nothing passes the ceiling
          const c = this.ceil;
          yl = clampf(yl, -c, c); yr = clampf(yr, -c, c);
          // ---- dry path aligned to the full latency (for delta and bypass) ----
          const ww = this.ww;
          this.raw[0][ww] = inL; this.raw[1][ww] = inR;
          const di = (ww - lat + MAXD) & (MAXD - 1);
          const dl = this.raw[0][di], dr = this.raw[1][di];
          this.ww = (ww + 1) & (MAXD - 1);
          const pr = Math.max(Math.abs(yl), Math.abs(yr)); if (pr > this.redPk) this.redPk = pr;
          if (this.delta){ yl = dl * this.pushG - yl; yr = dr * this.pushG - yr; }
          yl *= this.outG; yr *= this.outG;
          yl += (dl - yl) * this.byp; yr += (dr - yr) * this.byp;
          oL[s] = yl; oR[s] = yr;
          const po = Math.max(Math.abs(yl), Math.abs(yr)); if (po > this.outPk) this.outPk = po;
        }
        this.mCount += e - b0;
      }
      if (this.mCount >= this.fs / 30){
        this.mCount = 0;
        // gr = total peak reduction (clipper + limiter), lim = limiter gain alone; push = input peak after Push
        const red = this.pushPk > 1e-6 ? Math.min(1, this.redPk / this.pushPk) : 1;
        if (this.onMeter) this.onMeter({ in:this.inPk, out:this.outPk, push:this.pushPk, gr:red, lim:this.grMin });
        this.inPk = 0; this.outPk = 0; this.pushPk = 0; this.redPk = 0; this.grMin = 1;
      }
    }
  }
  return { Engine, DEFAULTS, MODES, latencyFor, lookaheadFor };
}
