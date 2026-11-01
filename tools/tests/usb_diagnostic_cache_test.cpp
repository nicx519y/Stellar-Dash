#include <cassert>
#include "usb_diagnostic_cache.hpp"
int main() {
    UsbDiagnosticCache cache{};
    assert(!cache.snapshot(0).valid && cache.snapshot(0).stale);
    uint8_t page[16]={1,2,0,4};
    usb_diag_put32(page+8,7);
    assert(cache.update(true,page,16,2,0,0xfffffff0u));
    assert(cache.valid && !cache.stale && cache.connectionGeneration==7);
    assert(!cache.snapshot(10).stale); // time wraps safely
    assert(cache.snapshot(1010).stale);
    assert(!cache.update(false,nullptr,0,3,0,2000));
    assert(cache.stale && cache.sampledAtMs==0xfffffff0u && cache.connectionGeneration==7);
    assert(!cache.update(true,page,16,3,0,2000)); // wrong page cannot refresh old data
    page[0]=2;assert(!cache.update(true,page,16,2,0,2000));
    page[0]=1;page[3]=5;assert(!cache.update(true,page,16,2,0,2000));
    page[3]=4;usb_diag_put32(page+8,8);
    assert(cache.update(true,page,16,2,0,2000));
    assert(!cache.stale && cache.connectionGeneration==8 && cache.sampledAtMs==2000);
}
