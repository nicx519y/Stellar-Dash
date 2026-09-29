import json
import shutil
import struct
import subprocess
import tempfile
import unittest
from types import SimpleNamespace
from pathlib import Path
from tools import boot_profile as bp

ROOT = Path(__file__).resolve().parents[2]


def fixture(events, status=1):
    header = [bp.MAGIC, 1, len(events), status, 64000000, 480000000,
              64000000, 0, 48000, 1, 200, 300, 8, 0, 4, 7]
    data = bytearray(1024)
    struct.pack_into("<16I", data, 0, *header)
    for index, (tag, tick, cycles) in enumerate(events):
        struct.pack_into("<3I", data, 64+index*12, tag, tick, cycles)
    return data


class BootProfileTest(unittest.TestCase):
    def test_v4_runtime_nested_spans(self):
        d = 2 << 16
        events = [(1, 0, 0), ((1 << 16)+33, 249, 10),
                  ((4 << 16)+107, bp.NO_TICK, 20)]
        events += [(d+tag, tick, 30) for tag, tick in [
            (108, 0), (109, 1), (110, 8), (111, 20),
            (220, 500), (208, 500), (209, 542), (209+bp.END, 1242),
            (210, 1242), (210+bp.END, 1400), (208+bp.END, 1400),
            (211, 1400), (211+bp.END, 1420), (212, 1420), (212+bp.END, 1520),
            (214, 1520), (214+bp.END, 1521), (213, 1521), (213+bp.END, 1600),
            (220+bp.END, 1607), (112, 1608)]]
        data = fixture(events)
        struct.pack_into("<I", data, 4, 4)
        result = bp.decode(data)
        self.assertTrue(result["complete"])
        self.assertNotIn("unfinished_stage", result["warnings"])
        rows = {r["stage"]: r for r in result["rows"]}
        self.assertEqual(rows["app_ch585_ready_wait"]["inclusive_ms"], 700)
        self.assertEqual(rows["app_ch585_start"]["exclusive_ms"], 42)
        self.assertEqual(rows["app_state_input"]["inclusive_ms"], 1107)
        self.assertEqual(rows["app_state_input"]["exclusive_ms"], 7)
        self.assertEqual(rows["application_board_done_to_application_loop_entry"]["exclusive_ms"], 481)

    def test_cpp_scope_closes_on_early_return_and_disabled_build(self):
        gxx = shutil.which("g++")
        self.assertIsNotNone(gxx, "host g++ required")
        with tempfile.TemporaryDirectory() as folder:
            p = Path(folder)
            p.joinpath("scope.cpp").write_text(r'''
#include <cassert>
#include "boot_profile.h"
static unsigned events[8], count, calls;
#if HBOX_BOOT_PROFILE
extern "C" void BootProfile_AppDetail(uint32_t tag) { events[count++]=tag; }
#endif
static bool child() { BP_APP_SCOPE(BP_APP_USB_PREPARE); ++calls; return false; }
static bool parent() { BP_APP_SCOPE(BP_APP_STATE_WEB_CONFIG); return child(); }
int main() {
 assert(!parent() && calls==1);
#if HBOX_BOOT_PROFILE
 assert(count==4 && events[0]==BP_APP_STATE_WEB_CONFIG);
 assert(events[1]==BP_APP_USB_PREPARE && events[2]==(BP_APP_USB_PREPARE|BP_END));
 assert(events[3]==(BP_APP_STATE_WEB_CONFIG|BP_END));
#else
 assert(count==0);
 (void)events;
#endif
}
''')
            for enabled in (0, 1):
                exe = p/f"scope-{enabled}.exe"
                subprocess.run([gxx, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    f"-DHBOX_BOOT_PROFILE={enabled}", "-I"+str(ROOT/"common"),
                    str(p/"scope.cpp"), "-o", str(exe)],
                    check=True, capture_output=True, timeout=60)
                subprocess.run([str(exe)], check=True, capture_output=True, timeout=10)

    def test_v3_nested_app_details_and_remainder(self):
        d = 2 << 16
        events = [(1, 0, 0), ((1 << 16)+33, 252, 10),
                  ((4 << 16)+107, bp.NO_TICK, 20)]
        events += [(d+tag, tick, 30) for tag, tick in [
            (108, 0), (109, 1), (110, 8), (111, 20),
            (207, 100), (204, 200), (204+bp.END, 900), (207+bp.END, 1100), (112, 1220)]]
        data = fixture(events)
        struct.pack_into("<I", data, 4, 3)
        result = bp.decode(data)
        self.assertTrue(result["complete"])
        rows = {row["stage"]: row for row in result["rows"]}
        self.assertEqual(rows["app_screen_setup"]["inclusive_ms"], 700)
        self.assertEqual(rows["app_state_enter"]["inclusive_ms"], 1000)
        self.assertEqual(rows["app_state_enter"]["exclusive_ms"], 300)
        self.assertEqual(rows["app_state_enter"]["app_init_share_percent"], 25)
        self.assertIsNone(rows["app_state_enter"]["boot_share_percent"])
        total = rows["application_board_done_to_application_loop_entry"]
        self.assertEqual(total["inclusive_ms"], 1200)
        self.assertEqual(total["exclusive_ms"], 200)
        struct.pack_into("<I", data, 52, 16)
        self.assertIn("application_details_omitted_capacity", bp.decode(data)["warnings"])

    def test_v2_app_completion_tick_intervals_and_clock_gap(self):
        events = [(1, 0, 0), ((1 << 16)+33, 249, 100),
                  ((4 << 16)+107, bp.NO_TICK, 200)]
        events += [((2 << 16)+tag, tick, 300)
                   for tag, tick in [(108, 0), (109, 1), (110, 8), (111, 20), (112, 620)]]
        data = fixture(events)
        struct.pack_into("<I", data, 4, 2)
        result = bp.decode(data)
        self.assertTrue(result["complete"])
        rows = {row["stage"]: row for row in result["rows"]}
        gap = rows["application_board_begin_to_application_clock_ready"]
        self.assertIsNone(gap["inclusive_ms"])
        self.assertEqual(gap["quality"], "application_clock_transition_unmeasured")
        self.assertEqual(rows["application_clock_ready_to_application_board_done"]["inclusive_ms"], 12)
        self.assertEqual(rows["application_board_done_to_application_loop_entry"]["inclusive_ms"], 600)
        self.assertEqual(result["boot_to_teardown_ms"], 249)
        struct.pack_into("<I", data, 8, len(events)-1)
        self.assertFalse(bp.decode(data)["complete"])
        struct.pack_into("<I", data, 8, len(events))
        struct.pack_into("<I", data, 64+3*12, (2 << 16)+109)
        self.assertFalse(bp.decode(data)["complete"])

    def test_capture_rejects_artifact_mismatch_before_device_access(self):
        with tempfile.TemporaryDirectory() as folder:
            manifest = Path(folder, "artifact-manifest.json")
            manifest.write_text(json.dumps(dict(bootSecurityMode="unlocked-development",
                targetSlot="A", files={"bootloader.bin":dict(bytes=3, sha256="wrong")})))
            Path(folder, "bootloader.bin").write_bytes(b"abc")
            args = SimpleNamespace(kind="reset", power_cycled=False, count=1, manifest=str(manifest))
            with self.assertRaisesRegex(ValueError, "hash/length"):
                bp.run_capture(args)
            args.kind = "cold"
            with self.assertRaisesRegex(ValueError, "real manual"):
                bp.run_capture(args)

    def test_clocks_ticks_and_wrap(self):
        h = dict(initial_hz=64000000, boot_hz=480000000, app_hz=64000000, flags=0)
        a = dict(domain=1, tick=10, cycles=0xFFFF0000)
        b = dict(domain=1, tick=11, cycles=(0xFFFF0000+480000) & 0xFFFFFFFF)
        self.assertEqual(bp.elapsed(a, b, h), (1.0, "DWT_single_wrap"))
        self.assertEqual(bp.elapsed(a, b, h, clock_change=True)[0], 1)
        b["domain"] = 4
        self.assertIsNone(bp.elapsed(a, b, h)[0])
        a["tick"] = b["tick"] = bp.NO_TICK
        a["domain"] = b["domain"] = 3
        self.assertIsNone(bp.elapsed(a, b, h)[0])
        self.assertEqual(bp.elapsed(a, b, h, upper_bound_ms=100)[0], 1)
        self.assertIsNone(bp.elapsed(a, b, h, upper_bound_ms=10000)[0])

    def test_nested_spans_are_exclusive_not_double_counted(self):
        d = 1 << 16
        data = fixture([(1, 0, 0), (d+7, 0, 0), (d+8, 1, 480000),
                        (d+8+bp.END, 3, 1440000), (d+7+bp.END, 4, 1920000),
                        (d+33, 4, 1920000), ((4<<16)+107, bp.NO_TICK, 2000000)])
        result = bp.decode(data)
        parent = next(x for x in result["rows"] if x["stage"] == "qspi_total")
        self.assertEqual(parent["inclusive_ms"], 4)
        self.assertEqual(parent["exclusive_ms"], 2)
        self.assertEqual(parent["boot_share_percent"], 50)
        self.assertTrue(result["complete"])
        self.assertFalse(result["baseline_eligible"])  # authentication stages absent

    def test_corrupt_partial_overflow(self):
        with self.assertRaises(ValueError):
            bp.decode(bytes(1023))
        with self.assertRaises(ValueError):
            bp.decode(bytes(1024))
        data = fixture([], status=2)
        self.assertIn("overflow", bp.decode(data)["warnings"])
        struct.pack_into("<I", data, 8, 81)
        with self.assertRaises(ValueError):
            bp.decode(data)

    def test_implausible_cycle_count_rejected(self):
        h = dict(initial_hz=64000000, boot_hz=480000000, app_hz=64000000, flags=0)
        a = dict(domain=1, tick=0, cycles=0)
        b = dict(domain=1, tick=1, cycles=48000000)
        self.assertEqual(bp.elapsed(a, b, h)[1], "cycle_tick_disagreement")

    def test_collector_has_only_bounded_sram_reads_and_ordinary_reset(self):
        script = bp.capture_script("0123456789ABCDEF01234567", "test.bin", reset=True)
        commands = [line.strip() for line in script.splitlines() if not line.lstrip().startswith("#")]
        self.assertEqual([x for x in commands if x.startswith("mww")],
                         ["mww 0xE000ED0C 0x05FA0004"])
        for forbidden in ("halt", "reset halt", "flash bank", "stm32h7x", "load_image", "program "):
            self.assertNotIn(forbidden, "\n".join(commands))
        self.assertNotIn("mww", bp.capture_script("0123456789ABCDEF01234567", "test.bin", reset=False))
        with self.assertRaises(ValueError):
            bp.capture_script("bad; shutdown", "test.bin", reset=True)

    def test_statistics_keep_cohorts_and_failure_samples(self):
        with tempfile.TemporaryDirectory() as folder:
            for i in range(3):
                profile = dict(baseline_eligible=True, rows=[dict(stage="test", exclusive_ms=i+1)],
                               boot_to_teardown_ms=i+1, log_ms=0)
                record = dict(kind="reset", phase="smoke" if i==0 else "baseline",
                              manifest_sha256="buildA", profile=profile)
                Path(folder, f"{i}.json").write_text(json.dumps(record))
            Path(folder, "bad.json").write_text(json.dumps(dict(kind="cold", error="timeout")))
            result = bp.summarize(folder)
            rows = [r for r in result["summary"] if r["stage"] == "test"]
            self.assertEqual(len(rows), 2)
            baseline = next(r for r in rows if r["phase"]=="baseline")
            self.assertEqual(baseline["median_ms"], 2.5)
            self.assertEqual(baseline["p95_ms"], 3)
            self.assertTrue(baseline["insufficient_samples"])
            self.assertEqual(len(result["failures"]), 1)

    def test_c_recorder_bounds_and_commit(self):
        gcc = shutil.which("gcc")
        self.assertIsNotNone(gcc, "host gcc required")
        with tempfile.TemporaryDirectory() as folder:
            p = Path(folder)
            p.joinpath("stm32h7xx_hal.h").write_text(r'''
#include <stdint.h>
#include <assert.h>
typedef struct {uint32_t CYCCNT, LAR, CTRL;} DWT_Type;
typedef struct {uint32_t DEMCR;} CoreDebug_Type;
typedef struct {uint32_t RSR, D1CFGR;} RCC_Type;
typedef struct {uint32_t CCR;} SCB_Type;
extern DWT_Type dwt; extern CoreDebug_Type core; extern RCC_Type rcc;
extern SCB_Type scb; extern unsigned cleans;
extern uint32_t SystemCoreClock, tick;
#define DWT (&dwt)
#define CoreDebug (&core)
#define RCC (&rcc)
#define SCB (&scb)
#define SCB_CCR_DC_Msk 1u
static inline void SCB_CleanDCache_by_Addr(uint32_t *address, int32_t size) {
 assert((uintptr_t)address == 0x3800F800u && size == 1024);
 ++cleans;
}
#define RCC_D1CFGR_D1CPRE 15
#define CoreDebug_DEMCR_TRCENA_Msk 1
#define DWT_CTRL_CYCCNTENA_Msk 1
#define __DSB() (++dwt.CYCCNT)
#define __ISB() (++dwt.CYCCNT)
#define __DMB() ((void)0)
static inline uint32_t HAL_GetTick(void) {return tick;}
static inline uint32_t HAL_RCC_GetSysClockFreq(void) {return 64000000;}
''')
            p.joinpath("test.c").write_text(r'''
#include <assert.h>
#include "stm32h7xx_hal.h"
#include "boot_profile.h"
DWT_Type dwt; CoreDebug_Type core; RCC_Type rcc;
SCB_Type scb; unsigned cleans;
uint32_t SystemCoreClock=64000000, tick;
extern volatile BootProfile hbox_boot_profile;
int main(void) {
 BootProfile_Init();
 assert(sizeof(BootProfile)==1024 && hbox_boot_profile.count==1);
 assert(hbox_boot_profile.reserved==1);
 SystemCoreClock=480000000; BootProfile_ClockReady();
 assert(hbox_boot_profile.count==1 && hbox_boot_profile.overhead_samples==8);
 int calls=0; int value=BP_CALL(BP_POWER, ++calls);
 assert(value==1 && calls==1 && hbox_boot_profile.count==3);
 BootProfile_Main();
 assert(hbox_boot_profile.version==4 && hbox_boot_profile.status==0);
 hbox_boot_profile.count=75; /* full authentication path still fits */
 BootProfile_AppTick(BP_APP_HAL_READY);
 assert(cleans==0);
 scb.CCR=SCB_CCR_DC_Msk;
 BootProfile_AppTick(BP_APP_BOARD_BEGIN);
 BootProfile_AppTick(BP_APP_CLOCK_READY);
 BootProfile_AppTick(BP_APP_BOARD_DONE);
 BootProfile_AppComplete();
 assert(hbox_boot_profile.count==80 && cleans==5);
 assert(hbox_boot_profile.status==1);
 unsigned count=hbox_boot_profile.count; BootProfile_Mark(2);
 assert(hbox_boot_profile.count==count);
 BootProfile_Init(); assert(hbox_boot_profile.reserved==2);
 BootProfile_AttestationResult(0); assert(hbox_boot_profile.flags & 8);
 for(unsigned i=0;i<BP_CAPACITY+5;i++) BootProfile_Mark(2);
 assert(hbox_boot_profile.count==BP_CAPACITY && hbox_boot_profile.status==2);
 BootProfile_Main(); assert(hbox_boot_profile.status==2);
 BootProfile_AppComplete(); assert(hbox_boot_profile.status==2);
 BootProfile_Init(); BootProfile_Main(); hbox_boot_profile.count=63;
 calls=0;
 value=BP_APP_CALL(BP_APP_STAGING_CHECK, ++calls);
 assert(value==1 && calls==1 && hbox_boot_profile.count==65);
 for(unsigned i=BP_APP_MODE_SAMPLE;i<=BP_APP_STATE_ENTER;i++) {
   BP_APP_RUN(i, ++calls);
 }
 assert(calls==8 && hbox_boot_profile.count==79 && !(hbox_boot_profile.flags & 16));
 BootProfile_AppComplete();
 assert(hbox_boot_profile.count==80 && hbox_boot_profile.status==1);
 /* A ninth span (retry/fallback) rolls back all optional detail records. */
 BootProfile_Init(); BootProfile_Main(); hbox_boot_profile.count=63;
 for(unsigned i=0;i<8;i++) { BP_APP_RUN(BP_APP_CH585_SELECT, ++calls); }
 assert(hbox_boot_profile.count==79);
 BP_APP_RUN(BP_APP_CH585_SELECT, ++calls);
 assert(hbox_boot_profile.count==63 && (hbox_boot_profile.flags & 16));
 BootProfile_AppComplete();
 assert(hbox_boot_profile.count==64 && hbox_boot_profile.status==1);
 assert((hbox_boot_profile.events[63].tag & 0xFFFF)==BP_APP_LOOP);
 BootProfile_Init(); BootProfile_Main(); hbox_boot_profile.count=79;
 for(unsigned i=BP_APP_STAGING_CHECK;i<=BP_APP_STATE_ENTER;i++) {
   BP_APP_RUN(i, ++calls);
 }
 assert(hbox_boot_profile.count==79 && hbox_boot_profile.status==0 && (hbox_boot_profile.flags & 16));
 BootProfile_AppComplete();
 assert(hbox_boot_profile.count==80 && hbox_boot_profile.status==1);
 return 0;
}
''')
            exe = p/"test.exe"
            compile_cmd = [gcc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-DHBOX_BOOT_PROFILE=1",
                           "-I"+str(p), "-I"+str(ROOT/"common"), str(ROOT/"common/boot_profile.c"),
                           str(p/"test.c"), "-o", str(exe)]
            print("boot-profile host recorder: compile", flush=True)
            subprocess.run(compile_cmd, check=True, capture_output=True, timeout=60)
            print("boot-profile host recorder: run", flush=True)
            subprocess.run([str(exe)], check=True, capture_output=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
