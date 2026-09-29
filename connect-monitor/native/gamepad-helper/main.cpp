#include <windows.h>
#include <Xinput.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Gaming.Input.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>

using namespace winrt;
using namespace Windows::Data::Json;
using namespace Windows::Gaming::Input;

struct Device {
    std::wstring id, name;
    unsigned vid{}, pid{}, generation{};
    Gamepad pad{nullptr};
    int slot{-1};
};
static JsonValue str(std::wstring const& value) { return JsonValue::CreateStringValue(value); }
static JsonValue num(unsigned value) { return JsonValue::CreateNumberValue(value); }
static unsigned xinputMask(XINPUT_GAMEPAD const& g) {
    constexpr WORD bits[] = {0x1000,0x2000,0x4000,0x8000,0x100,0x200,0,0,0x20,0x10,0x40,0x80,1,2,4,8};
    unsigned mask=0;
    for(unsigned i=0;i<16;++i) if(g.wButtons & bits[i]) mask |= 1u<<i;
    if(g.bLeftTrigger>=128) mask|=1u<<6;
    if(g.bRightTrigger>=128) mask|=1u<<7;
    return mask;
}
static unsigned wgiMask(GamepadReading const& g) {
    constexpr GamepadButtons bits[] = {GamepadButtons::A, GamepadButtons::B, GamepadButtons::X, GamepadButtons::Y,
        GamepadButtons::LeftShoulder, GamepadButtons::RightShoulder, GamepadButtons::None, GamepadButtons::None,
        GamepadButtons::View, GamepadButtons::Menu, GamepadButtons::LeftThumbstick, GamepadButtons::RightThumbstick,
        GamepadButtons::DPadUp, GamepadButtons::DPadDown, GamepadButtons::DPadLeft, GamepadButtons::DPadRight};
    unsigned mask=0;
    for(unsigned i=0;i<16;++i) if((g.Buttons & bits[i]) != GamepadButtons::None) mask |= 1u<<i;
    if(g.LeftTrigger>=128.0/255.0) mask|=1u<<6;
    if(g.RightTrigger>=128.0/255.0) mask|=1u<<7;
    return mask;
}

int main() {
    init_apartment(apartment_type::multi_threaded);
    std::atomic_bool running{true};
    std::mutex selectionMutex;
    std::set<std::wstring> selected;
    unsigned revision=0, serial=0;
    // EOF is the parent lifetime signal. All Windows input calls stay on this thread.
    std::thread input([&] {
        init_apartment(apartment_type::multi_threaded);
        std::string line;
        while(std::getline(std::cin,line)) {
            if(line.size()>16384) break;
            try {
                auto command=JsonObject::Parse(to_hstring(line));
                std::set<std::wstring> next;
                for(auto const& id:command.GetNamedArray(L"ids")) next.emplace(id.GetString());
                std::lock_guard lock(selectionMutex);
                selected=std::move(next);
                revision=static_cast<unsigned>(command.GetNamedNumber(L"revision"));
            } catch(...) { /* Invalid commands never broaden selection. */ }
        }
        running=false;
    });
    std::map<std::wstring,Device> devices;
    auto nextScan=std::chrono::steady_clock::now();
    bool wgiAvailable=true;
    while(running) {
        auto started=std::chrono::steady_clock::now();
        try {
            if(started>=nextScan) {
                std::map<std::wstring,Device> next;
                wgiAvailable=true;
                try {
                    for(auto const& raw:RawGameController::RawGameControllers()) {
                        try {
                            auto pad=Gamepad::FromGameController(raw);
                            if(!pad || (raw.HardwareVendorId()==0xcafe && raw.HardwareProductId()==0x4021)) continue;
                            Device d;
                            d.id=L"wgi:"+std::wstring(raw.NonRoamableId());
                            d.name=raw.DisplayName(); d.vid=raw.HardwareVendorId(); d.pid=raw.HardwareProductId(); d.pad=pad;
                            auto old=devices.find(d.id);
                            d.generation=old!=devices.end() && old->second.pad==pad ? old->second.generation : ++serial;
                            next.emplace(d.id,std::move(d));
                        } catch(...) {}
                    }
                } catch(...) { wgiAvailable=false; }
                // Identity-less slots are exposed only for explicit fallback selection.
                if(!wgiAvailable || next.empty()) for(DWORD slot=0;slot<4;++slot) {
                    XINPUT_STATE state{};
                    if(XInputGetState(slot,&state)!=ERROR_SUCCESS) continue;
                    Device d; d.id=L"xinput:"+std::to_wstring(slot); d.name=L"Windows XInput "+std::to_wstring(slot+1);
                    d.slot=static_cast<int>(slot);
                    auto old=devices.find(d.id);
                    d.generation=old==devices.end()?++serial:old->second.generation;
                    next.emplace(d.id,std::move(d));
                }
                devices=std::move(next); nextScan=started+std::chrono::milliseconds(250);
            }
            std::set<std::wstring> ids;
            unsigned currentRevision;
            { std::lock_guard lock(selectionMutex); ids=selected; currentRevision=revision; }
            JsonArray inventory, readings;
            for(auto it=devices.begin();it!=devices.end();) {
                auto const& d=it->second;
                bool connected=true;
                unsigned mask=0;
                // Also check availability of unselected fallback slots so a disconnect revokes them.
                if(d.slot>=0) {
                    XINPUT_STATE state{}; connected=XInputGetState(static_cast<DWORD>(d.slot),&state)==ERROR_SUCCESS;
                    if(connected) mask=xinputMask(state.Gamepad);
                } else if(ids.contains(d.id)) {
                    try { mask=wgiMask(d.pad.GetCurrentReading()); } catch(...) { connected=false; }
                }
                if(!connected) { it=devices.erase(it); continue; }
                JsonObject info;
                info.Insert(L"id",str(d.id)); info.Insert(L"name",str(d.name));
                info.Insert(L"vendorId",num(d.vid)); info.Insert(L"productId",num(d.pid));
                info.Insert(L"generation",num(d.generation)); info.Insert(L"backend",str(d.slot<0?L"wgi":L"xinput"));
                inventory.Append(info);
                if(ids.contains(d.id)) {
                    JsonObject reading;
                    reading.Insert(L"id",str(d.id)); reading.Insert(L"generation",num(d.generation));
                    reading.Insert(L"standardMask",num(mask)); readings.Append(reading);
                }
                ++it;
            }
            JsonObject frame;
            frame.Insert(L"devices",inventory); frame.Insert(L"readings",readings);
            frame.Insert(L"revision",num(currentRevision)); frame.Insert(L"wgiAvailable",JsonValue::CreateBooleanValue(wgiAvailable));
            std::cout << to_string(frame.Stringify()) << '\n' << std::flush;
            if(!std::cout) break;
        } catch(hresult_error const& e) { std::cerr<<to_string(e.message())<<'\n'; break; }
        std::this_thread::sleep_until(started+std::chrono::microseconds(16667));
    }
    // On a broken output pipe the parent is gone; don't wait for a blocked stdin reader.
    if(running) ExitProcess(1);
    input.join();
    return 0;
}
