#!/usr/bin/env python3
"""Check bias-aware static authority, exact boundaries and source rejection."""
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import compile_stabilized_allocation as compiler
import screen_stabilized_net_authority as net_screen


ROOT = Path(__file__).resolve().parents[2]
PROFILE = Path(__file__).with_name('stabilized_allocation_synthetic.json')
SPECIFICATION = Path(__file__).with_name('stabilized_net_authority_synthetic.json')


class NetAuthorityTests(unittest.TestCase):
    def setUp(self):
        self.profile = json.loads(PROFILE.read_text())
        self.report = compiler.compile_profile(self.profile)
        self.report['input_sha256'] = hashlib.sha256(PROFILE.read_bytes()).hexdigest()
        self.specification = json.loads(SPECIFICATION.read_text())

    def screen(self):
        return net_screen.screen_net_box(self.profile, self.report, self.specification)

    def test_synthetic_zero_bias_net_box(self):
        result = self.screen()
        self.assertEqual(result['status'], 'sufficient_static_net_box_reachable')
        self.assertEqual(result['correction_axis_abs_bound_normalized'],
                         [dict(numerator=1, denominator=10)] * 3)
        self.assertEqual(result['correction_norm_squared'], dict(numerator=3, denominator=100))
        self.assertEqual(result['guaranteed_norm_squared'], dict(numerator=1, denominator=4))
        self.assertFalse(result['vehicle_qualified'])
        self.assertFalse(result['actual_zero_moment_trim_measured'])

    def test_nonzero_bias_uncertainty_and_exact_boundary(self):
        self.specification['bias_nm_frd'] = [-0.12, 0, 0]
        self.assertEqual(self.screen()['correction_norm_squared'],
                         dict(numerator=9, denominator=50))
        self.assertEqual(self.screen()['status'], 'sufficient_static_net_box_reachable')
        self.specification['bias_nm_frd'] = [-0.08, 0, 0]
        self.specification['bias_abs_uncertainty_nm_frd'] = [0.04, 0, 0]
        self.assertEqual(self.screen()['correction_norm_squared'],
                         dict(numerator=9, denominator=50))
        self.specification['bias_nm_frd'] = [0.16, 0, 0]
        self.specification['bias_abs_uncertainty_nm_frd'] = [0, 0, 0]
        self.assertEqual(self.screen()['status'], 'inconclusive_for_declared_net_box')
        self.specification['desired_net_moment_abs_bound_nm_frd'] = [0.04, 0, 0]
        self.assertEqual(self.screen()['correction_norm_squared'],
                         dict(numerator=1, denominator=4))
        self.assertEqual(self.screen()['status'], 'sufficient_static_net_box_reachable')

    def test_rank_and_actuator_margin_are_conservative(self):
        self.report['effectiveness_abs_uncertainty'] = [[0.25] * 4 for _ in range(3)]
        self.assertEqual(self.screen()['status'], 'rank_not_proven_for_reported_intervals')
        self.report = compiler.compile_profile(self.profile)
        self.report['input_sha256'] = hashlib.sha256(PROFILE.read_bytes()).hexdigest()
        self.report['allocator_inputs']['maximum'][0] = 0.05
        self.assertEqual(self.screen()['status'], 'inconclusive_for_declared_net_box')

    def test_mock_measured_schema_never_qualifies_vehicle(self):
        self.profile['profile_kind'] = 'measured_single_condition'
        for canard in self.profile['canards']:
            canard['deflection_slope_uncertainty_rad_per_command'] = 0.001
            canard['moment_derivative_uncertainty_nm_per_rad'] = [0.01] * 3
        self.report = compiler.compile_profile(self.profile)
        self.report['input_sha256'] = 'a' * 64
        self.specification['profile_input_sha256'] = 'a' * 64
        self.specification['purpose'] = 'candidate_vehicle_requirement_unreviewed'
        with self.assertRaisesRegex(ValueError, 'uncertainty must be positive'):
            self.screen()
        self.specification['bias_abs_uncertainty_nm_frd'] = [0.001] * 3
        result = self.screen()
        self.assertEqual(result['status'], 'sufficient_static_net_box_reachable')
        self.assertFalse(result['vehicle_qualified'])
        self.assertFalse(result['physical_acceptance'])

    def test_invalid_specification_rejected(self):
        cases = (
            ('profile_input_sha256', '0' * 64),
            ('body_frame', 'FLU'),
            ('condition', dict(self.specification['condition'], airflow_mps=25)),
            ('canard_trim_commands', [0.1, 0, 0, 0]),
            ('purpose', 'candidate_vehicle_requirement_unreviewed'),
            ('bias_nm_frd', [float('nan'), 0, 0]),
            ('bias_abs_uncertainty_nm_frd', [-0.01, 0, 0]),
            ('desired_net_moment_abs_bound_nm_frd', [-0.04, 0, 0]),
            ('desired_net_moment_abs_bound_nm_frd', [0, 0, 0]),
            ('provenance', {'bias': self.specification['provenance']['bias']}))
        for field, value in cases:
            altered = copy.deepcopy(self.specification)
            altered[field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                net_screen.screen_net_box(self.profile, self.report, altered)

    def test_cli_binds_report_and_external_bias_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            compiled = work / 'compiled.json'
            compiled.write_text(json.dumps(self.report, indent=2) + '\n')
            output = work / 'net.json'
            command = [sys.executable, str(Path(net_screen.__file__)), '--input', str(PROFILE),
                       '--compiled', str(compiled), '--evidence-root', str(ROOT),
                       '--net-demand', str(SPECIFICATION), '--output', str(output)]
            self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertEqual(json.loads(output.read_text())['status'],
                             'sufficient_static_net_box_reachable')
            tampered = copy.deepcopy(self.report)
            tampered['allocator_inputs']['effectiveness'][0][0] = 0.3
            compiled.write_text(json.dumps(tampered, indent=2) + '\n')
            command[-1] = str(work / 'rejected-report.json')
            rejected = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(rejected.returncode, 2)
            self.assertIn('compiled report differs', rejected.stderr)
            self.assertFalse(Path(command[-1]).exists())
            compiled.write_text(json.dumps(self.report, indent=2) + '\n')
            bad_spec = copy.deepcopy(self.specification)
            bad_spec['provenance']['bias']['sha256'] = '0' * 64
            bad_path = work / 'bad-spec.json'
            bad_path.write_text(json.dumps(bad_spec) + '\n')
            command[command.index('--net-demand') + 1] = str(bad_path)
            command[-1] = str(work / 'rejected-bias.json')
            rejected = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(rejected.returncode, 2)
            self.assertIn('provenance hash mismatch', rejected.stderr)
            self.assertFalse(Path(command[-1]).exists())


if __name__ == '__main__':
    unittest.main()
