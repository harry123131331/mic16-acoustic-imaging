"""核对真实协议的周期闭合、必要条件和候选/源数边界，避免将预算写成实测。"""
import unittest,math,sys
from pathlib import Path
tools_dir=Path(__file__).resolve().parents[1]/'scripts'
if tools_dir.exists():sys.path.insert(0,str(tools_dir))
from calculate_realtime_budget import output_bounds,network_budget,enhancement_work,scenario

class BudgetContractTests(unittest.TestCase):
    def test_sample_clock_closure(self):
        total=0;last=0;counts=set()
        for w in range(625):
            a,b=output_bounds(w);self.assertEqual(a,last);last=b;total+=b-a;counts.add(b-a)
            self.assertEqual(math.ceil((b-a)/128),132)
        self.assertEqual(counts,{16777,16778});self.assertEqual(total,10485760)

    def test_zero_source_is_manifest(self):
        r=network_budget(0,0)
        self.assertEqual(r['audio_payload_Mbps'],0);self.assertEqual(r['audio_packets_per_window'],1)
        self.assertGreater(r['audio_wire_Mbps'],0)

    def test_video_wire_with_actual_header(self):
        r=network_budget(8,5)
        self.assertAlmostEqual(r['video_wire_Mbps'],34.95936)
        self.assertEqual(r['audio_payload_Mbps'],8.192)
        self.assertGreater(r['audio_wire_Mbps'],8.192)
        self.assertEqual(r['audio_packets_per_window'],1056)

    def test_application_limiter_before_line_limit(self):
        r=network_budget(0,10)
        self.assertGreater(r['total_application_UDP_Bps'],8000000)
        self.assertLess(r['total_wire_lower_bound_Mbps'],100)
        self.assertIn('超过当前应用层发送配额',r['failed_necessary_conditions'])

    def test_video_alone_fails_100mbps_at_15fps(self):
        self.assertGreater(network_budget(0,15)['video_wire_Mbps'],100)

    def test_transient_id_union_and_pacing(self):
        r=network_budget(24,5)
        self.assertEqual(r['audio_packets_per_window'],3168)
        self.assertAlmostEqual(r['audio_min_interpacket_span_s'],.79175)
        self.assertFalse(r['audio_span_is_additional_CPU_time'])

    def test_good_static_budget_is_not_acceptance(self):
        r=network_budget(8,5)
        self.assertEqual(r['status'],'STATIC_NECESSARY_CONDITIONS_ONLY_RUNTIME_UNKNOWN')
        self.assertFalse(r['continuous_realtime_passed']);self.assertFalse(r['measured'])

    def test_missing_enhancement_measurements_not_zero(self):
        r=enhancement_work(16,8)
        self.assertIsNone(r['actual_enhancement_s']);self.assertIsNone(r['actual_full_pipeline_s'])
        self.assertEqual(r['potential_prediction_only_saved_FFT512'],1608)
        self.assertEqual(r['channel_weight_FFT4096'],320)
        self.assertFalse(r['weights_prepared_per_source'])

    def test_no_source_skips_enhancement_but_not_detection(self):
        r=enhancement_work(16,0)
        self.assertEqual(r['channel_weight_IFFT4096'],0)
        self.assertTrue(scenario()['scene_specific_detector_cost_unknown'])

    def test_candidate_and_current_source_domains(self):
        with self.assertRaises(ValueError):enhancement_work(1,2)
        with self.assertRaises(ValueError):enhancement_work(16,9)
        with self.assertRaises(ValueError):enhancement_work(True,0)

    def test_invalid_network_parameters(self):
        for args in [(-1,5),(25,5),(True,5),(1,-1),(1,float('nan')),(1,float('inf')),(1,31)]:
            with self.assertRaises(ValueError):network_budget(*args)
        with self.assertRaises(ValueError):network_budget(1,5,application_rate=0)
        with self.assertRaises(ValueError):network_budget(1,5,link_mbps=float('nan'))

    def test_scenario_coverage(self):
        r=scenario();self.assertEqual(len(r['network']),17)
        self.assertEqual([x['confirmed_sources'] for x in r['enhancement']],[0,1,2,4,8])

if __name__=='__main__':unittest.main(verbosity=2)
