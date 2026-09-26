"""Execute the production Tcl recovery with a simulated debug adapter.

No OpenOCD subprocess or hardware access. Faults include init after asserting
NRST, target examination, halt, and a failed attempt to release NRST.
"""
import tkinter
import unittest

from tools.build import BuildTool


class Stm32ResetRecoveryTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tcl = tkinter.Tcl()
        self.tcl.eval("""
            set events {}
            set held 0
            set deferred 0
            set srst_declared 0
            set fault none
            proc fail_at {stage} {
                if {$::fault eq $stage} {error "injected $stage failure"}
            }
            proc reset_config {args} {
                set ::srst_declared [expr {[lsearch -exact $args srst_only] >= 0}]
                if {[lsearch -exact $args connect_assert_srst] < 0} {
                    error "recovery must assert reset during connection"
                }
            }
            proc target {op} {return fixture.cpu}
            proc gdb_port {value} {}
            proc tcl_port {value} {}
            proc telnet_port {value} {}
            proc fixture.cpu {op args} {
                if {$op eq "configure"} {
                    set ::deferred [expr {[lsearch -exact $args -defer-examine] >= 0}]
                    return
                }
                if {$op ne "arp_examine"} {error "unexpected target operation"}
                lappend ::events examine
                if {$::held} {error "H7 AXI inaccessible under reset"}
                fail_at examine
            }
            proc init {} {
                lappend ::events init
                if {!$::srst_declared} {error "cannot assert undeclared SRST"}
                set ::held 1
                if {!$::deferred} {error "early H7 examination under reset"}
                fail_at init
            }
            proc adapter {op line} {
                if {$line ne "srst"} {error "unexpected adapter operation"}
                lappend ::events "$op $line"
                if {$op eq "assert"} {
                    set ::held 1
                    fail_at assert
                } elseif {$op eq "deassert"} {
                    fail_at deassert
                    set ::held 0
                } else {error "unexpected adapter operation"}
            }
            proc sleep {milliseconds} {}
            proc halt {timeout} {
                lappend ::events halt
                if {$::held} {error "halt attempted under reset"}
                fail_at halt
            }
        """)

    def execute(self) -> None:
        self.tcl.eval("\n".join([
            *BuildTool._openocd_reset_recovery_commands(),
            "lappend ::events identity_check",
            "lappend ::events flash_stage",
        ]))

    def events(self):
        return self.tcl.splitlist(self.tcl.eval("set events"))

    def test_release_and_halt_precede_callers_identity_and_flash_stages(self):
        self.execute()
        events = self.events()
        self.assertLess(events.index("deassert srst"), events.index("examine"))
        self.assertLess(events.index("examine"), events.index("halt"))
        self.assertLess(events.index("halt"), events.index("identity_check"))
        self.assertLess(events.index("identity_check"), events.index("flash_stage"))
        self.assertEqual(self.tcl.eval("set held"), "0")

    def test_failures_release_reset_and_block_all_later_stages(self):
        for stage in ("init", "assert", "examine", "halt"):
            with self.subTest(stage=stage):
                self.setUp()
                self.tcl.setvar("fault", stage)
                with self.assertRaisesRegex(tkinter.TclError, "STM32 reset recovery failed"):
                    self.execute()
                self.assertEqual(self.tcl.eval("set held"), "0")
                self.assertIn("deassert srst", self.events())
                self.assertNotIn("identity_check", self.events())
                self.assertNotIn("flash_stage", self.events())

    def test_release_failure_retries_cleanup_but_never_continues(self):
        self.tcl.setvar("fault", "deassert")
        with self.assertRaisesRegex(tkinter.TclError, "injected deassert failure"):
            self.execute()
        self.assertEqual(self.events().count("deassert srst"), 2)
        self.assertNotIn("examine", self.events())
        self.assertNotIn("flash_stage", self.events())


if __name__ == "__main__":
    unittest.main()
