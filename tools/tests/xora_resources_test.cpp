#include "sha256_simple.h"
#include "xora_factory_resources.hpp"
#include "xora_resource_store.hpp"
#include "xora_resource_policy.hpp"
#include "xora_test_topology.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>
using namespace XoraResource;
struct Flash {
  std::array<uint8_t, 65536> bytes;
  int fail = -1, writes = 0, readFail = -1, reads = 0;
  bool partial = false;
  Flash() { bytes.fill(255); }
};
bool read(void *c, uint32_t o, uint8_t *b, size_t n) {
  auto &f = *static_cast<Flash *>(c);
  if (f.readFail == f.reads++)
    return false;
  if (o + n > f.bytes.size())
    return false;
  memcpy(b, f.bytes.data() + o, n);
  return true;
}
bool erase(void *c, uint32_t o, size_t n) {
  auto &f = *static_cast<Flash *>(c);
  if (f.fail == f.writes++) {
    if (f.partial)
      memset(f.bytes.data() + o, 255, n / 2);
    return false;
  }
  assert(o >= 0x3000 && o + n <= 0xf000);
  memset(f.bytes.data() + o, 255, n);
  return true;
}
bool program(void *c, uint32_t o, const uint8_t *b, size_t n) {
  auto &f = *static_cast<Flash *>(c);
  const bool failed = f.fail == f.writes++;
  if (failed && !f.partial)
    return false;
  assert(o >= 0x3000 && o + n <= 0xf000);
  for (unsigned i = 0; i < (failed ? n / 2 : n); i++) {
    assert((f.bytes[o + i] & b[i]) == b[i]);
    f.bytes[o + i] &= b[i];
  }
  return !failed;
}
bool verify(const uint8_t *b, size_t n) {
  uint8_t h[32];
  sha256_simple_ctx_t c;
  sha256_simple_init(&c);
  sha256_simple_update(&c, b, 32);
  sha256_simple_update(&c, b + 64, n - 64);
  sha256_simple_final(&c, h);
  return !memcmp(h, b + 32, 32);
}
IO io(Flash &f) { return {&f, read, erase, program, verify}; }
void testPolicy() {
  Flash flash;
  Store store;
  assert(store.load(io(flash)));
  for (const auto &f : factory) assert(store.change(f.bytes, f.size));
  assert(installedCount(store, false) == 6 && installedCount(store, true) == 4);
  Light light;
  auto make = [&](unsigned revision) {
    std::vector<uint8_t> b(factory[8].bytes, factory[8].bytes + factory[8].size);
    put32(b.data() + 8, revision);
    sha256_simple_ctx_t c;
    sha256_simple_init(&c); sha256_simple_update(&c,b.data(),32);
    sha256_simple_update(&c,b.data()+64,b.size()-64); sha256_simple_final(&c,b.data()+32);
    assert(parseLight(b.data(),b.size(),light)); return b;
  };
  for (unsigned revision = 2; revision <= 3; ++revision) {
    auto b = make(revision); assert(!installError(store,light,b.size())); assert(store.change(b.data(),b.size()));
  }
  auto b = make(4);
  assert(!strcmp(installError(store,light,b.size()),"RESOURCE_COUNT_LIMIT"));
  b = make(3); assert(!installError(store,light,b.size())); // same revision is idempotent
  Light ambient; assert(parseLight(factory[0].bytes,factory[0].size,ambient)); ambient.ref.revision=9;
  assert(!strcmp(installError(store,ambient,capacity),"RESOURCE_STORAGE_FULL"));
  Ref ref = {}; strcpy(ref.id,"key-ripple"); ref.revision=1;
  for (int partial = 0; partial < 2; ++partial)
    for (int failure = 0; failure < 98; ++failure) {
      Flash test = flash;
      test.writes = 0; test.fail = failure; test.partial = partial;
      Store writer; assert(writer.load(io(test)));
      ProfileRefs profile[1] = {{ref, staticRef(true)}};
      ProfileRefs persisted[1] = {profile[0]};
      auto result = removeReferences(ref, false, profile, true,
          [&] { persisted[0] = profile[0]; return true; }, [] {},
          [&] { return writer.change(nullptr,0,&ref); });
      assert(result.configurationSaved && sameRef(persisted[0].keys, staticRef(false)));
      test.fail = -1;
      Store reboot; assert(reboot.load(io(test))); unsigned n = 0;
      assert(bool(reboot.find(ref,n)) == !result.removed);
      assert(reboot.find(persisted[0].keys,n));
    }
  ProfileRefs refs[3] = {};
  for (auto &p : refs) {p.keys=ref;p.ambient=staticRef(true);}
  unsigned saves=0,reloads=0,deletes=0;
  auto failed=removeReferences(ref,false,refs,true,[&]{++saves;return false;},[&]{++reloads;},[&]{++deletes;return true;});
  assert(!failed.configurationSaved && !failed.removed && saves==1 && reloads==0 && deletes==0);
  for (auto &p : refs) assert(sameRef(p.keys,ref));
  auto partial=removeReferences(ref,false,refs,true,[&]{++saves;return true;},[&]{assert(saves==2);++reloads;},[&]{assert(reloads==1);++deletes;return false;});
  assert(partial.configurationSaved && !partial.removed);
  for (unsigned i=0;i<3;++i) {assert(partial.affected[i]);assert(sameRef(refs[i].keys,staticRef(false)));assert(sameRef(refs[i].ambient,staticRef(true)));}
  auto done=removeReferences(ref,false,refs,true,[]{assert(false);return false;},[]{assert(false);},[&]{return store.change(nullptr,0,&ref);});
  assert(done.removed);
  Store restarted; assert(restarted.load(io(flash))); unsigned ignored=0;
  assert(!restarted.find(ref,ignored));
  auto protectedResult=removeReferences(staticRef(false),false,refs,true,[]{assert(false);return false;},[]{assert(false);},[]{assert(false);return false;});
  assert(!strcmp(protectedResult.error,"RESOURCE_PROTECTED"));
  puts("Resource quota, default protection, multi-profile save-before-delete and failure boundaries passed");
}
int main(int argc, char **argv) {
  if (argc > 2) {
    std::ifstream input(argv[2], std::ios::binary);
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(input)), {});
    Light l;
    if (!strcmp(argv[1], "validate")) {
      printf("%d\n", int(b.size() >= 64 && verify(b.data(), b.size()) && parseLight(b.data(), b.size(), l)));
      return 0;
    }
    assert(verify(b.data(), b.size()) && parseLight(b.data(), b.size(), l));
    Engine engine;
    engine.load(l, 0);
    Color colors[62] = {};
    engine.render(300, 1, testPoints, 62, 22, true, false, 3, l.colors, colors);
    puts(
        "External composition parsed and rendered by the unchanged C++ engine");
    return 0;
  }
  if (argc > 1) {
    for (unsigned j = 0; j < factoryCount; j++)
      for (unsigned scenario = 0; scenario < 20; scenario++) {
        Light l;
        assert(parseLight(factory[j].bytes, factory[j].size, l));
        Engine e;
        e.load(l, 0, 1);
        for (unsigned t = 0; t < 120; t++) {
          Color colors[62] = {};
          uint32_t mask = t % 10 < 2 ? 0x3fffff : t % 10 < 5 ? 3 : 0;
          e.render(t * 137, mask, testPoints, 62, 22, scenario % 2,
                   scenario / 2 % 2, scenario / 4 + 1, l.colors, colors);
          printf("%u,%u,%u", j, scenario, t);
          for (const auto &c : colors)
            printf(",%u,%u,%u", c.r, c.g, c.b);
          puts("");
        }
      }
    return 0;
  }
  testPolicy();
  for (const auto &f : factory) {
    Light l;
    assert(parseLight(f.bytes, f.size, l) && verify(f.bytes, f.size));
  }
  Flash flash;
  Store s;
  assert(s.load(io(flash)) && s.blank());
  assert(s.change(factory[0].bytes, factory[0].size));
  Flash baseline = flash;
  for (bool partial : {false, true})
    for (int failure = 0; failure < 98; failure++) {
      Flash test = baseline;
      test.fail = failure;
      test.partial = partial;
      test.writes = 0;
      Store writer;
      assert(writer.load(io(test)));
      bool committed = writer.change(factory[1].bytes, factory[1].size);
      test.fail = -1;
      Store reboot;
      assert(reboot.load(io(test)));
      assert(reboot.count() == (committed ? 2u : 1u));
      for (unsigned i = 0; i < 0x3000; i++)
        assert(test.bytes[i] == 255);
      for (unsigned i = 0xf000; i < 65536; i++)
        assert(test.bytes[i] == 255);
    }
  for (int failure = 0; failure < 99; ++failure) {
    Flash test = baseline;
    test.reads = 0;
    test.readFail = failure;
    Store writer;
    bool loaded = writer.load(io(test));
    if (failure < 2) {
      assert(!loaded && !writer.ready());
      assert(!writer.change(factory[1].bytes, factory[1].size));
    } else {
      assert(loaded);
      assert(!writer.change(factory[1].bytes, factory[1].size));
    }
    test.readFail = -1;
    Store reboot;
    assert(reboot.load(io(test)));
    // A failure reading the final committed marker can report an error while
    // leaving a complete, valid new bank; all earlier read failures keep A.
    assert(reboot.count() == (failure == 98 ? 2u : 1u));
  }
  assert(s.change(factory[0].bytes, factory[0].size) && s.count() == 1);
  uint8_t corrupt[2048];
  memcpy(corrupt, factory[0].bytes, factory[0].size);
  corrupt[90] ^= 1;
  assert(!s.change(corrupt, factory[0].size));
  for (unsigned i = 1; i < factoryCount; i++)
    assert(s.change(factory[i].bytes, factory[i].size));
  assert(s.count() == 10);
  Light l;
  assert(parseLight(factory[0].bytes, factory[0].size, l));
  assert(s.change(nullptr, 0, &l.ref));
  assert(s.count() == 9);
  Flash bad;
  bad.bytes[0x3000] = 0;
  Store damaged;
  assert(!damaged.load(io(bad)));
  assert(!damaged.change(factory[0].bytes, factory[0].size));
  Flash full;
  Store bounded;
  assert(bounded.load(io(full)));
  for (unsigned revision = 1; revision <= 33; ++revision) {
    std::vector<uint8_t> bytes(factory[0].bytes,
                               factory[0].bytes + factory[0].size);
    put32(bytes.data() + 8, revision);
    sha256_simple_ctx_t ctx;
    sha256_simple_init(&ctx);
    sha256_simple_update(&ctx, bytes.data(), 32);
    sha256_simple_update(&ctx, bytes.data() + 64, bytes.size() - 64);
    sha256_simple_final(&ctx, bytes.data() + 32);
    assert(bounded.change(bytes.data(), bytes.size()) ==
           (revision <= maxEntries));
  }
  assert(bounded.count() == 32);
  puts("XORA resource engine, digest, real journal fault injection and bounds "
       "passed");
}
