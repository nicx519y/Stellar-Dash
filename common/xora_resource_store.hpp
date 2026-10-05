#pragma once
#include "xora_light_engine.hpp"

namespace XoraResource {
constexpr uint32_t bankBytes = 0x6000, payloadStart = 0x1000,
                   capacity = bankBytes - payloadStart, maxEntries = 32;
constexpr uint32_t bankOffsets[2] = {0x3000, 0x9000};
static_assert(bankOffsets[0] + bankBytes <= bankOffsets[1] &&
                  bankOffsets[1] + bankBytes <= 0x10000,
              "Resource banks overlap");
struct IO {
  void *context;
  bool (*read)(void *, uint32_t, uint8_t *, size_t);
  bool (*erase)(void *, uint32_t, size_t);
  bool (*program)(void *, uint32_t, const uint8_t *, size_t);
  bool (*verify)(const uint8_t *, size_t);
};
inline uint32_t bankCrc(const uint8_t *b) {
  uint32_t c = ~0u;
  for (unsigned i = 0; i < bankBytes; i++) {
    if (i >= 20 && i < 32)
      continue;
    c ^= b[i];
    for (unsigned j = 0; j < 8; j++)
      c = (c >> 1) ^ (0xedb88320u & uint32_t(-int32_t(c & 1)));
  }
  return ~c;
}
class Store {
  IO io_ = {};
  uint8_t banks_[2][bankBytes] = {};
  int active_ = -1;
  bool ready_ = false, blank_ = false;
  bool valid(const uint8_t *b) {
    if (le32(b) != 0x42535258 || le32(b + 4) != 1 || !le32(b + 8) ||
        le32(b + 12) > maxEntries || le32(b + 16) > capacity ||
        le32(b + 24) != 0x58434d54 || le32(b + 20) != bankCrc(b))
      return false;
    unsigned end = payloadStart;
    for (unsigned i = 0; i < le32(b + 12); i++) {
      unsigned o = le16(b + 64 + i * 4), n = le16(b + 66 + i * 4);
      Light l;
      if (o != end || n > maxBytes || o + n > bankBytes ||
          !parseLight(b + o, n, l) || !io_.verify(b + o, n))
        return false;
      for (unsigned j = 0; j < i; j++) {
        unsigned old = le16(b + 64 + j * 4);
        if (le32(b + old + 8) == l.ref.revision &&
            !memcmp(b + old + 16, l.ref.id, 16))
          return false;
      }
      end += n;
    }
    return end == payloadStart + le32(b + 16);
  }
  bool commit() {
    int next = active_ == 0 ? 1 : 0;
    uint8_t *b = banks_[next];
    put32(b, 0x42535258);
    put32(b + 4, 1);
    uint32_t generation = active_ < 0 ? 1 : le32(banks_[active_] + 8) + 1;
    put32(b + 8, generation ? generation : 1);
    put32(b + 24, 0xffffffff);
    put32(b + 20, bankCrc(b));
    uint32_t address = bankOffsets[next];
    if (!io_.erase(io_.context, address, bankBytes))
      return false;
    // Both header and body are staged; the commit word remains erased.
    for (unsigned o = 0; o < bankBytes; o += 256)
      if (!io_.program(io_.context, address + o, b + o, 256))
        return false;
    uint8_t check[256];
    for (unsigned o = 0; o < bankBytes; o += 256)
      if (!io_.read(io_.context, address + o, check, 256) ||
          memcmp(check, b + o, 256))
        return false;
    uint8_t marker[4];
    put32(marker, 0x58434d54);
    if (!io_.program(io_.context, address + 24, marker, 4) ||
        !io_.read(io_.context, address + 24, check, 4) ||
        memcmp(check, marker, 4))
      return false;
    put32(b + 24, 0x58434d54);
    active_ = next;
    ready_ = true;
    blank_ = false;
    return true;
  }

public:
  bool load(IO io) {
    io_ = io;
    active_ = -1;
    ready_ = false;
    blank_ = true;
    bool good[2] = {};
    for (unsigned i = 0; i < 2; i++) {
      if (!io_.read(io_.context, bankOffsets[i], banks_[i], bankBytes))
        return false;
      good[i] = valid(banks_[i]);
      for (unsigned j = 0; j < bankBytes; j++)
        if (banks_[i][j] != 0xff)
          blank_ = false;
    }
    if (good[0])
      active_ = 0;
    if (good[1] &&
        (active_ < 0 || int32_t(le32(banks_[1] + 8) - le32(banks_[0] + 8)) > 0))
      active_ = 1;
    ready_ = active_ >= 0 || blank_;
    return ready_;
  }
  bool ready() const { return ready_; }
  bool blank() const { return blank_; }
  unsigned count() const {
    return active_ < 0 ? 0 : le32(banks_[active_] + 12);
  }
  unsigned used() const { return active_ < 0 ? 0 : le32(banks_[active_] + 16); }
  const uint8_t *entry(unsigned i, unsigned &n) const {
    if (i >= count())
      return nullptr;
    const uint8_t *b = banks_[active_];
    n = le16(b + 66 + i * 4);
    return b + le16(b + 64 + i * 4);
  }
  const uint8_t *find(const Ref &ref, unsigned &n) const {
    for (unsigned i = 0; i < count(); i++) {
      const uint8_t *p = entry(i, n);
      if (le32(p + 8) == ref.revision && !memcmp(p + 16, ref.id, 16))
        return p;
    }
    return nullptr;
  }
  bool change(const uint8_t *added, unsigned size,
              const Ref *removed = nullptr) {
    if (!ready_)
      return false;
    Light light;
    if (added && (!parseLight(added, size, light) || !io_.verify(added, size)))
      return false;
    unsigned oldSize = 0;
    if (added) {
      const uint8_t *old = find(light.ref, oldSize);
      if (old)
        return oldSize == size && !memcmp(old, added, size);
    }
    int next = active_ == 0 ? 1 : 0;
    uint8_t *b = banks_[next];
    memset(b, 0xff, bankBytes);
    unsigned num = 0, end = payloadStart;
    for (unsigned i = 0; i < count(); i++) {
      unsigned n = 0;
      const uint8_t *p = entry(i, n);
      if (removed && le32(p + 8) == removed->revision &&
          !memcmp(p + 16, removed->id, 16))
        continue;
      memcpy(b + end, p, n);
      b[64 + num * 4] = uint8_t(end);
      b[65 + num * 4] = uint8_t(end >> 8);
      b[66 + num * 4] = uint8_t(n);
      b[67 + num * 4] = uint8_t(n >> 8);
      num++;
      end += n;
    }
    if (added) {
      if (num == maxEntries || end + size > bankBytes)
        return false;
      memcpy(b + end, added, size);
      b[64 + num * 4] = uint8_t(end);
      b[65 + num * 4] = uint8_t(end >> 8);
      b[66 + num * 4] = uint8_t(size);
      b[67 + num * 4] = uint8_t(size >> 8);
      num++;
      end += size;
    }
    put32(b + 12, num);
    put32(b + 16, end - payloadStart);
    return commit();
  }
};
} // namespace XoraResource
