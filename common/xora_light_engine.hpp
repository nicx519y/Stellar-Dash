#pragma once
#include <cmath>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace XoraResource {
constexpr unsigned maxBytes = 2048, maxLayers = 8, maxEvents = 5, maxLeds = 62;
struct Ref {
  char id[16];
  uint32_t revision;
};
struct ProfileRefs {
  Ref keys;
  Ref ambient;
};
struct Layer {
  uint8_t signal, from, to, blend, mask;
  float p[6];
};
struct Light {
  Ref ref;
  char name[80];
  uint8_t kind, clock, count;
  uint32_t colors[3];
  Layer layers[maxLayers];
};
struct Point {
  float x, y;
};
struct Color {
  uint8_t r, g, b;
};
inline uint16_t le16(const uint8_t *b) { return b[0] | uint16_t(b[1]) << 8; }
inline uint32_t le32(const uint8_t *b) {
  return le16(b) | uint32_t(le16(b + 2)) << 16;
}
inline void put32(uint8_t *b, uint32_t v) {
  for (unsigned i = 0; i < 4; i++)
    b[i] = uint8_t(v >> (i * 8));
}
inline float real(const uint8_t *b) {
  uint32_t v = le32(b);
  float f;
  memcpy(&f, &v, 4);
  return f;
}
inline bool utf8(const uint8_t *b, unsigned n) {
  for (unsigned i = 0; i < n;) {
    uint32_t c = b[i++], min = 0;
    unsigned more = 0;
    if (c < 0x80) {
      if (c < 0x20 || c == 0x7f)
        return false;
      continue;
    }
    if (c >= 0xc2 && c <= 0xdf) {
      more = 1;
      c &= 31;
      min = 0x80;
    } else if (c >= 0xe0 && c <= 0xef) {
      more = 2;
      c &= 15;
      min = 0x800;
    } else if (c >= 0xf0 && c <= 0xf4) {
      more = 3;
      c &= 7;
      min = 0x10000;
    } else
      return false;
    if (i + more > n)
      return false;
    while (more--) {
      if ((b[i] & 0xc0) != 0x80)
        return false;
      c = (c << 6) | (b[i++] & 63);
    }
    if (c < min || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
      return false;
  }
  return true;
}
inline bool parseLight(const uint8_t *b, size_t n, Light &out) {
  if (!b || n < 192 || n > maxBytes || le32(b) != 0x53455258 ||
      le16(b + 4) != 1 || b[7] != 1 || (b[6] != 2 && b[6] != 3) ||
      le16(b + 12) + 64u != n || !b[14] || b[14] > 15 || b[15] || !le32(b + 8))
    return false;
  for (unsigned i = 0; i < 16; i++) {
    if (i < b[14]) {
      char c = b[16 + i];
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-'))
        return false;
    } else if (b[16 + i])
      return false;
  }
  Light value = {};
  memcpy(value.ref.id, b + 16, 16);
  value.ref.revision = le32(b + 8);
  value.kind = b[6];
  value.count = b[76];
  value.clock = b[77];
  if (!value.count || value.count > maxLayers || value.clock > 2 ||
      n != 160u + value.count * 32u || !b[78] || b[78] > 79 || b[79])
    return false;
  if (!utf8(b + 80, b[78]))
    return false;
  for (unsigned i = b[78]; i < 80; i++)
    if (b[80 + i])
      return false;
  memcpy(value.name, b + 80, b[78]);
  for (unsigned i = 0; i < 3; i++) {
    value.colors[i] = le32(b + 64 + i * 4);
    if (value.colors[i] > 0xffffff)
      return false;
  }
  for (unsigned i = 0; i < value.count; i++) {
    const uint8_t *p = b + 160 + i * 32;
    Layer &l = value.layers[i];
    l.signal = p[0];
    l.from = p[1];
    l.to = p[2];
    l.blend = p[3];
    l.mask = p[4];
    if (l.signal > 10 || l.from > 2 || l.to > 2 || l.blend > 2 || l.mask > 2 ||
        p[5] || p[6] || p[7])
      return false;
    for (unsigned j = 0; j < 6; j++) {
      l.p[j] = real(p + 8 + j * 4);
      if (!std::isfinite(l.p[j]) || std::fabs(l.p[j]) > 10000)
        return false;
    }
    if (l.p[0] <= 0 || l.p[1] <= 0)
      return false;
  }
  out = value;
  return true;
}
inline Color color(uint32_t n) {
  return {uint8_t(n >> 16), uint8_t(n >> 8), uint8_t(n)};
}
inline float clamp(float n) { return fmaxf(0, fminf(1, n)); }
inline Color mix(Color a, Color b, float t) {
  t = clamp(t);
  return {uint8_t(a.r * (1 - t) + b.r * t), uint8_t(a.g * (1 - t) + b.g * t),
          uint8_t(a.b * (1 - t) + b.b * t)};
}
inline const char *legacyId(bool ambient, unsigned index) {
  static const char *keys[] = {"key-static", "key-breath", "key-star",
                               "key-flow",   "key-ripple", "key-transform"};
  static const char *around[] = {"ambient-static", "ambient-breath",
                                 "ambient-quake", "ambient-meteor"};
  return ambient ? around[index < 4 ? index : 0] : keys[index < 6 ? index : 0];
}
struct Event {
  uint32_t start;
  uint8_t center;
};
class Engine {
  Light light_ = {};
  uint32_t start_ = 0, lastMask_ = 0, trigger_ = 0, rng_ = 1, cycle_ = 0,
           lastHalf_ = 0xffffffff;
  float previous_ = 0;
  Event events_[maxEvents] = {};
  unsigned count_ = 0;
  uint8_t stars_[2][3] = {{0}}, starCount_[2] = {0};
  uint8_t passed_[maxLayers][maxLeds] = {};
  uint32_t layerCycles_[maxLayers] = {};
  float layerPrevious_[maxLayers] = {};
  uint32_t random() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
  }

public:
  void load(const Light &l, uint32_t now, uint32_t seed = 1) {
    light_ = l;
    start_ = trigger_ = now;
    lastMask_ = cycle_ = count_ = 0;
    lastHalf_ = 0xffffffff;
    previous_ = 0;
    rng_ = seed ? seed : 1;
    memset(starCount_, 0, sizeof(starCount_));
    memset(passed_, 0, sizeof(passed_));
    memset(layerCycles_, 0, sizeof(layerCycles_));
    memset(layerPrevious_, 0, sizeof(layerPrevious_));
  }
  const Light &resource() const { return light_; }
  void render(uint32_t now, uint32_t mask, const Point *points, unsigned total,
              unsigned keyCount, bool sync, bool oneShot, uint8_t speed,
              const uint32_t palette[3], Color *output) {
    if (!light_.count || total > maxLeds || !total || keyCount > total ||
        keyCount > 32)
      return;
    speed = speed < 1 ? 1 : speed > 5 ? 5 : speed;
    uint32_t pressed = mask & ~lastMask_;
    lastMask_ = mask;
    if (pressed)
      trigger_ = now;
    for (unsigned i = 0; i < keyCount; i++)
      if (pressed & (1u << i)) {
        if (count_ == maxEvents) {
          memmove(events_, events_ + 1, sizeof(Event) * (maxEvents - 1));
          count_--;
        }
        events_[count_++] = {now, uint8_t(i)};
      }
    unsigned keep = 0;
    for (unsigned i = 0; i < count_; i++)
      if (now - events_[i].start < 3000u / speed)
        events_[keep++] = events_[i];
    count_ = keep;
    uint32_t duration =
        light_.clock == 0 ? 10000u
                          : 600u * (7u - speed) / (light_.clock == 2 ? 2u : 1u);
    uint32_t elapsed = now - (oneShot ? trigger_ : start_);
    float phase = oneShot ? fminf(1, float(elapsed) / duration)
                          : float(elapsed % duration) / duration;
    if (light_.clock == 0)
      phase = fmodf(phase * speed, 1);
    if (phase < previous_ && previous_ > .8f) {
      cycle_++;
      memset(passed_, 0, sizeof(passed_));
    }
    previous_ = phase;
    unsigned begin = light_.kind == 3 ? keyCount : 0,
             end = light_.kind == 3 || sync ? total : keyCount;
    if (end <= begin)
      return;
    float minX = points[begin].x, maxX = minX;
    for (unsigned i = begin; i < end; i++) {
      minX = fminf(minX, points[i].x);
      maxX = fmaxf(maxX, points[i].x);
    }
    float center = (minX + maxX) / 2;
    minX -= 100;
    maxX += 100;
    float fast = fmodf(phase * 2, 1);
    unsigned half = fast < .5f ? 0 : 1;
    if (half != lastHalf_) {
      uint8_t available[maxLeds];
      unsigned n = 0;
      for (unsigned i = begin; i < end; i++) {
        bool used = false;
        for (unsigned g = 0; g < 2; g++)
          for (unsigned j = 0; j < starCount_[g]; j++)
            used |= stars_[g][j] == i;
        if (!used)
          available[n++] = i;
      }
      starCount_[half] = uint8_t(fminf(float(n), float(2 + random() % 2)));
      for (unsigned j = 0; j < starCount_[half]; j++) {
        unsigned k = random() % n;
        stars_[half][j] = available[k];
        available[k] = available[--n];
      }
      lastHalf_ = half;
    }
    float eventRadius[maxEvents] = {};
    for (unsigned e = 0; e < count_; e++) {
      Point origin = points[events_[e].center];
      float far = 0;
      for (unsigned k = 0; k < total; k++)
        far =
            fmaxf(far, hypotf(points[k].x - origin.x, points[k].y - origin.y));
      eventRadius[e] =
          float(now - events_[e].start) / (3000u / speed) * far * 1.1f;
    }
    for (unsigned j = 0; j < light_.count; j++) {
      float p = fmodf(phase * light_.layers[j].p[0], 1);
      if (p < layerPrevious_[j]) {
        layerCycles_[j]++;
        memset(passed_[j], 0, sizeof(passed_[j]));
      }
      layerPrevious_[j] = p;
    }
    for (unsigned i = begin; i < end; i++) {
      Color result = {};
      bool down = i < keyCount && (mask & (1u << i));
      for (unsigned j = 0; j < light_.count; j++) {
        const Layer &l = light_.layers[j];
        if ((l.mask == 1 && !down) || (l.mask == 2 && down))
          continue;
        float t = 0, p = fmodf(phase * l.p[0], 1);
        Color a = color(palette[l.from]), b = color(palette[l.to]);
        switch (l.signal) {
        case 0:
          t = 0;
          break;
        case 1:
          t = oneShot && phase >= 1 ? 0 : sinf(p * 3.14159265358979323846f);
          break;
        case 2: {
          float d =
              fabsf(points[i].x - (minX + (maxX - minX) * p * l.p[2])) / l.p[1];
          t = d <= 1 ? 1 - d * d * (3 - 2 * d) : 0;
          break;
        }
        case 3:
          for (unsigned e = 0; e < count_; e++) {
            Point origin = points[events_[e].center];
            float radius = eventRadius[e];
            float d = fabsf(radius - hypotf(points[i].x - origin.x,
                                            points[i].y - origin.y));
            if (d < l.p[1])
              t = fmaxf(t, cosf(d / l.p[1] * 1.57079632679f));
          }
          break;
        case 4: {
          for (unsigned g = 0; g < 2; g++)
            for (unsigned k = 0; k < starCount_[g]; k++)
              if (stars_[g][k] == i) {
                float q = fmodf(fast + (g ? .5f : 0), 1);
                t = fmaxf(t, sinf(q * 3.14159265358979323846f));
              }
          break;
        }
        case 5: {
          float x = minX + (maxX - minX) * p * l.p[2];
          if (x > points[i].x + l.p[1] / 2)
            passed_[j][i] = 1;
          bool odd = (layerCycles_[j] + passed_[j][i]) % 2;
          float q = clamp((points[i].x - (x - l.p[1] / 2)) / l.p[1]);
          q = q * q * (3 - 2 * q);
          t = odd ? 1 : 0;
          if (points[i].x >= x - l.p[1] / 2 && points[i].x <= x + l.p[1] / 2)
            t = odd ? q : 1 - q;
          break;
        }
        case 6: {
          unsigned n = end - begin, head = unsigned(p * n) % n,
                   d = (head + n - (i - begin)) % n;
          unsigned length = 2 + speed * 3;
          t = oneShot && phase >= 1 ? 0
              : d < length          ? 1 - float(d) / length
                                    : 0;
          break;
        }
        case 7: {
          float radius =
              (p < .4f ? p / .4f : 1 - (p - .4f) / .6f) * (maxX - minX) / 2;
          t = oneShot && phase >= 1
                  ? 0
                  : clamp((radius - fabsf(points[i].x - center)) / l.p[1]);
          break;
        }
        case 8:
          t = down ? 1 : 0;
          break;
        case 9:
          t = 1 - fabsf(2 * p - 1);
          break;
        case 10:
          t = p < .5f ? clamp(p * 2) : clamp((1 - p) * 2);
          break;
        }
        Color c = mix(a, b, t);
        if (l.blend == 0)
          result = c;
        else if (l.blend == 1)
          result = {uint8_t(fminf(255, result.r + c.r)),
                    uint8_t(fminf(255, result.g + c.g)),
                    uint8_t(fminf(255, result.b + c.b))};
        else
          result = {uint8_t(fmaxf(result.r, c.r)),
                    uint8_t(fmaxf(result.g, c.g)),
                    uint8_t(fmaxf(result.b, c.b))};
      }
      output[i] = result;
    }
  }
};
} // namespace XoraResource
