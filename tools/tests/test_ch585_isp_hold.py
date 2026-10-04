"""Evaluate the production Tcl against mocked registers; no hardware access."""
import re
import tkinter
import unittest
from tools import ch585_isp_hold as hold


class IspHoldTests(unittest.TestCase):
    def interpreter(self, *, wrong_chip=False, uid=(0x12345678,0x12341234,0xabcdef01)):
        tcl=tkinter.Tcl()
        registers={hold.DBGMCU:0x450 if not wrong_chip else 0x413,
                   hold.DBGMCU+4:0x20,hold.DBGMCU+0x34:0x10,hold.DBGMCU+0x54:0x20,
                   hold.RCC_AHB4ENR:0x55}
        for base in (hold.GPIO_E,hold.GPIO_I):
            for off in (0,4,0xc,0x14): registers[base+off]=0x5a5a5a5a
        for i,value in enumerate(uid): registers[0x1ff1e800+4*i]=value
        before=dict(registers); writes=[]; events=[]
        def write(addr,value):
            address,value=int(addr,0),int(value,0)
            writes.append((address,value)); events.append(('write',address,value))
            if address in (hold.GPIO_E+0x18,hold.GPIO_I+0x18):
                odr=address-4
                registers[odr]=(registers[odr] & ~(value>>16)) | (value&0xffff)
            else: registers[address]=value
        tcl.createcommand('mrw',lambda addr:str(registers[int(addr,0)]))
        tcl.createcommand('mww',write)
        tcl.createcommand('sleep',lambda ms:events.append(('sleep',int(ms))))
        tcl.createcommand('echo',lambda text:events.append(('echo',text)))
        for name in ('source','find','transport','adapter','gdb_port','tcl_port','telnet_port',
                     'swj_newdap','dap','target','init','halt','wait_halt','mdw','shutdown'):
            tcl.createcommand(name,lambda *args: '')
        return tcl,registers,before,writes,events

    def test_cpu_halt_power_hold_spi_park_and_preserved_gpio(self):
        tcl,regs,before,writes,events=self.interpreter()
        tcl.eval(hold.hold_script())
        self.assertEqual(regs[hold.GPIO_I+0x14] & (hold.MAIN|hold.TX|hold.HOST),hold.MAIN|hold.TX)
        allowed_pins=hold.MAIN|hold.TX|hold.HOST
        self.assertEqual(regs[hold.GPIO_I+0x14]&~allowed_pins,before[hold.GPIO_I+0x14]&~allowed_pins)
        for base,pins in [(hold.GPIO_I,(4,10,13)),(hold.GPIO_E,(5,11,12,14))]:
            mask=hold.mode_mask(pins)
            for off in (0,0xc): self.assertEqual(regs[base+off]&~mask,before[base+off]&~mask)
        self.assertEqual(regs[hold.GPIO_E]&hold.mode_mask((5,12,14)),hold.mode_mask((5,12,14)))
        self.assertEqual(regs[hold.GPIO_E]&(3<<22),1<<22)
        self.assertTrue(regs[hold.GPIO_E+0x14]&hold.NSS)
        self.assertEqual(regs[hold.GPIO_I+4]&allowed_pins,0)
        self.assertEqual(regs[hold.RCC_AHB4ENR],before[hold.RCC_AHB4ENR]|0x110)
        self.assertEqual(regs[hold.DBGMCU+0x54],before[hold.DBGMCU+0x54]|0x40000)
        bsrr=[v for addr,v in writes if addr==hold.GPIO_I+0x18]
        self.assertEqual(bsrr,[hold.MAIN|((hold.TX|hold.HOST)<<16),hold.TX])
        self.assertTrue(all(not (value&(hold.MAIN<<16)) for value in bsrr))
        off=events.index(('write',hold.GPIO_I+0x18,bsrr[0]))
        wait=events.index(('sleep',50)); on=events.index(('write',hold.GPIO_I+0x18,hold.TX))
        self.assertLess(off,wait);self.assertLess(wait,on)
        self.assertTrue(any(e[0]=='echo' and 'TX_ISP_HOLD_READY:' in e[1] for e in events))
        self.assertTrue(all(addr in {hold.DBGMCU+4,hold.DBGMCU+0x34,hold.DBGMCU+0x54,
                                    hold.RCC_AHB4ENR,hold.GPIO_E,hold.GPIO_E+4,hold.GPIO_E+0xc,hold.GPIO_E+0x18,
                                    hold.GPIO_I,hold.GPIO_I+4,hold.GPIO_I+0xc,hold.GPIO_I+0x18} for addr,_ in writes))

    def test_wrong_target_or_uid_stops_before_gpio_writes(self):
        for options,expected in [({'wrong_chip':True},None),({'uid':(0,0,0)},None),
                                 ({'uid':(0xffffffff,)*3},None),({},'1'*24)]:
            tcl,_,_,writes,events=self.interpreter(**options)
            with self.assertRaises(tkinter.TclError): tcl.eval(hold.hold_script(expected))
            self.assertEqual(writes,[])
            self.assertFalse(any(e[0]=='echo' and 'READY:' in e[1] for e in events))
        with self.assertRaises(ValueError): hold.hold_script('bad')

    def test_config_does_not_reset_resume_or_register_flash_banks(self):
        lines=hold.hold_script().splitlines()
        self.assertIn('halt',lines)
        for line in lines:
            self.assertIsNone(re.match(r'\s*(flash|reset|resume|program|load_image|stm32h7x)\b',line))
        self.assertLess(lines.index('halt'),next(i for i,s in enumerate(lines) if s.startswith('mww')))
        hold.validate_board()

    def test_readback_failure_never_reports_ready(self):
        tcl,_,_,_,events=self.interpreter()
        def wrong_gpio(addr):
            address=int(addr,0)
            if address==hold.DBGMCU: return str(0x450)
            if 0x1ff1e800<=address<=0x1ff1e808: return '123'
            return '0'
        tcl.createcommand('mrw',wrong_gpio)
        with self.assertRaises(tkinter.TclError): tcl.eval(hold.hold_script())
        self.assertFalse(any(e[0]=='echo' and 'READY:' in e[1] for e in events))


if __name__=='__main__': unittest.main()
