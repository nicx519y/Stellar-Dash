"use strict";
const clamp = (n) => Math.max(0, Math.min(1, n));
const rgb = (n) => [(n >>> 16) & 255, (n >>> 8) & 255, n & 255];
class LightEngine {
  constructor(source, now = 0, seed = 1) {
    this.source = source;
    this.start = this.trigger = now;
    this.lastMask = 0;
    this.rng = seed || 1;
    this.cycle = 0;
    this.lastHalf = -1;
    this.previous = 0;
    this.events = [];
    this.stars = [[], []];
    this.passed = Array.from({ length: 8 }, () => new Set());
    this.layerCycles = Array(8).fill(0);
    this.layerPrevious = Array(8).fill(0);
  }
  random() {
    let x = this.rng;
    x ^= x << 13;
    x ^= x >>> 17;
    x ^= x << 5;
    this.rng = x >>> 0;
    return this.rng;
  }
  render(
    now,
    mask,
    points,
    keyCount,
    {
      sync = false,
      oneShot = false,
      speed = 1,
      colors = this.source.payload.colors,
    } = {},
  ) {
    const f = Math.fround,
      payload = this.source.payload,
      total = points.length;
    if (!total || total > 62) return [];
    speed = Math.max(1, Math.min(5, speed));
    const pressed = mask & ~this.lastMask;
    this.lastMask = mask;
    if (pressed) this.trigger = now;
    for (let i = 0; i < keyCount; i++)
      if (pressed & (1 << i)) this.events.push({ start: now, center: i });
    this.events = this.events
      .filter((e) => now - e.start < Math.floor(3000 / speed))
      .slice(-5);
    const duration =
        payload.clock === "key-cycle"
          ? 10000
          : (600 * (7 - speed)) /
            (payload.clock === "half-ambient-cycle" ? 2 : 1),
      elapsed = now - (oneShot ? this.trigger : this.start);
    let phase = oneShot
      ? Math.min(1, f(elapsed / duration))
      : f((elapsed % duration) / duration);
    if (payload.clock === "key-cycle") phase = f(f(phase * speed) % 1);
    if (phase < this.previous && this.previous > 0.8) {
      this.cycle++;
      this.passed.forEach((s) => s.clear());
    }
    this.previous = phase;
    const begin = this.source.type === "ambient-lighting" ? keyCount : 0,
      end = this.source.type === "ambient-lighting" || sync ? total : keyCount;
    if (end <= begin) return Array.from({ length: total }, () => [0, 0, 0]);
    let minX = Math.min(...points.slice(begin, end).map((p) => p.x)),
      maxX = Math.max(...points.slice(begin, end).map((p) => p.x));
    const center = f((minX + maxX) / 2);
    minX -= 100;
    maxX += 100;
    const fast = f(f(phase * 2) % 1),
      half = fast < 0.5 ? 0 : 1;
    if (half !== this.lastHalf) {
      const available = Array.from(
        { length: end - begin },
        (_, i) => i + begin,
      ).filter((i) => !this.stars.flat().includes(i));
      const n = Math.min(available.length, 2 + (this.random() % 2));
      this.stars[half] = [];
      for (let j = 0; j < n; j++) {
        const k = this.random() % available.length;
        this.stars[half].push(available[k]);
        available[k] = available.at(-1);
        available.pop();
      }
      this.lastHalf = half;
    }
    payload.layers.forEach((l, j) => {
      const p = f(f(phase * l.params[0]) % 1);
      if (p < this.layerPrevious[j]) {
        this.layerCycles[j]++;
        this.passed[j].clear();
      }
      this.layerPrevious[j] = p;
    });
    const output = Array.from({ length: total }, () => [0, 0, 0]);
    for (let i = begin; i < end; i++) {
      const down = i < keyCount && !!(mask & (1 << i));
      let result = [0, 0, 0];
      for (const [j, l] of payload.layers.entries()) {
        if ((l.mask === 1 && !down) || (l.mask === 2 && down)) continue;
        let t = 0,
          p = f(f(phase * l.params[0]) % 1);
        const width = l.params[1];
        switch (l.signal) {
          case "constant":
            break;
          case "sine":
            t = oneShot && phase >= 1 ? 0 : Math.sin(p * Math.PI);
            break;
          case "scan": {
            const d =
              Math.abs(points[i].x - (minX + (maxX - minX) * p * l.params[2])) /
              width;
            t = d <= 1 ? 1 - d * d * (3 - 2 * d) : 0;
            break;
          }
          case "radial":
            for (const e of this.events) {
              const origin = points[e.center],
                far = Math.max(
                  ...points.map((q) =>
                    Math.hypot(q.x - origin.x, q.y - origin.y),
                  ),
                );
              const radius =
                  ((now - e.start) / Math.floor(3000 / speed)) * far * 1.1,
                d = Math.abs(
                  radius -
                    Math.hypot(points[i].x - origin.x, points[i].y - origin.y),
                );
              if (d < width)
                t = Math.max(t, Math.cos(((d / width) * Math.PI) / 2));
            }
            break;
          case "random-set":
            for (let g = 0; g < 2; g++)
              if (this.stars[g].includes(i))
                t = Math.max(
                  t,
                  Math.sin(((fast + (g ? 0.5 : 0)) % 1) * Math.PI),
                );
            break;
          case "scan-latch": {
            const x = minX + (maxX - minX) * p * l.params[2];
            if (x > points[i].x + width / 2) this.passed[j].add(i);
            const odd =
              (this.layerCycles[j] + Number(this.passed[j].has(i))) % 2;
            let q = clamp((points[i].x - (x - width / 2)) / width);
            q = q * q * (3 - 2 * q);
            t = odd ? 1 : 0;
            if (points[i].x >= x - width / 2 && points[i].x <= x + width / 2)
              t = odd ? q : 1 - q;
            break;
          }
          case "ring-tail": {
            const n = end - begin,
              head = Math.floor(p * n) % n,
              d = (head + n - (i - begin)) % n,
              length = 2 + speed * 3;
            t = oneShot && phase >= 1 ? 0 : d < length ? 1 - d / length : 0;
            break;
          }
          case "center-wave": {
            const radius =
              ((p < 0.4 ? p / 0.4 : 1 - (p - 0.4) / 0.6) * (maxX - minX)) / 2;
            t =
              oneShot && phase >= 1
                ? 0
                : clamp((radius - Math.abs(points[i].x - center)) / width);
            break;
          }
          case "pressed":
            t = down ? 1 : 0;
            break;
          case "triangle":
            t = 1 - Math.abs(2 * p - 1);
            break;
          case "envelope":
            t = p < 0.5 ? clamp(p * 2) : clamp((1 - p) * 2);
            break;
        }
        const a = rgb(colors[l.from]),
          b = rgb(colors[l.to]),
          c = a.map((n, k) =>
            Math.trunc(f(f(n * f(1 - clamp(t))) + f(b[k] * clamp(t)))),
          );
        result = c.map((n, k) =>
          l.blend === "add"
            ? Math.min(255, result[k] + n)
            : l.blend === "max"
              ? Math.max(result[k], n)
              : n,
        );
      }
      output[i] = result;
    }
    return output;
  }
}
module.exports = { LightEngine };
