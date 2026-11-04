"""核验寄存器错位、64位指针缺半、未知仿真周期等危险输入必须被区分。"""
import unittest,sys
from pathlib import Path
tools_dir=Path(__file__).resolve().parents[1]/'scripts'
if tools_dir.exists():sys.path.insert(0,str(tools_dir))
from audit_hls_contracts import verify_registers,parse_cosim

def fixture():
    ns='http://www.spiritconsortium.org/XMLSchema/SPIRIT/1685-2009'
    body=''.join('<register><name>'+name+'</name><addressOffset>'+str(offset)+'</addressOffset><size>32</size></register>'
                 for name,offset in [('ddr_arena_1',16),('ddr_arena_2',20),('frame_samples',44)])
    xml=('<component xmlns="'+ns+'"><registers>'+body+'</registers></component>').encode()
    driver='#define X_CONTROL_ADDR_DDR_ARENA_DATA 0x10\n#define X_CONTROL_BITS_DDR_ARENA_DATA 64\n#define X_CONTROL_ADDR_FRAME_SAMPLES_DATA 0x2c\n#define X_CONTROL_BITS_FRAME_SAMPLES_DATA 32\n'
    expected={'DDR_ARENA':(16,64),'FRAME_SAMPLES':(44,32)}
    return xml,driver,expected

class HLSAuditTests(unittest.TestCase):
    def test_normal_64_bit_argument(self):
        x,d,e=fixture();r=verify_registers(x,d,e);self.assertEqual(r['DDR_ARENA']['argument_bits'],64)

    def test_driver_offset_change_rejected(self):
        x,d,e=fixture()
        with self.assertRaises(ValueError):verify_registers(x,d.replace('0x2c','0x30'),e)

    def test_pointer_upper_register_rejected(self):
        x,d,e=fixture()
        with self.assertRaises(ValueError):verify_registers(x.replace(b'ddr_arena_2',b'ddr_arena_missing'),d,e)

    def test_xml_offset_change_rejected(self):
        x,d,e=fixture()
        with self.assertRaises(ValueError):verify_registers(x.replace(b'>44<',b'>48<'),d,e)

    def test_argument_width_change_rejected(self):
        x,d,e=fixture()
        with self.assertRaises(ValueError):verify_registers(x,d.replace('DATA 64','DATA 32'),e)

    def test_NA_latency_is_unknown(self):
        r=parse_cosim('| Verilog | Pass | NA | NA | NA | NA | NA | NA | NA |')
        self.assertIsNone(r['max_cycles']);self.assertIsNone(r['interval_cycles'])

    def test_failed_cosim_not_accepted(self):
        with self.assertRaises(ValueError):parse_cosim('| Verilog | Fail | 8 | 8 | 8 | NA | NA | NA | 8 |')

    def test_scaled_threshold_counterexample(self):
        raw=1.;gain=2**16
        self.assertGreater(raw,1e-8)
        self.assertLessEqual(raw/gain**2,1e-8)
        self.assertFalse(raw>gain**2*1e-8)

if __name__=='__main__':unittest.main(verbosity=2)
