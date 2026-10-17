#!/usr/bin/env python3
"""Exercise static box guarantee, conservative boundaries and source binding."""
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import compile_stabilized_allocation as compiler
import screen_stabilized_static_authority as authority


ROOT = Path(__file__).resolve().parents[2]
EXAMPLE = Path(__file__).with_name('stabilized_allocation_synthetic.json')
DEMAND = Path(__file__).with_name('stabilized_static_demand_synthetic.json')


class StaticAuthorityTests(unittest.TestCase):
    def setUp(self):
        self.profile = json.loads(EXAMPLE.read_text())
        self.report = compiler.compile_profile(self.profile)
        self.report['input_sha256'] = hashlib.sha256(EXAMPLE.read_bytes()).hexdigest()
        self.demand = json.loads(DEMAND.read_text())

    def test_synthetic_incremental_box_has_exact_static_guarantee(self):
        result = authority.screen_box(self.report, self.demand)
        self.assertEqual(result['status'], 'sufficient_static_incremental_box_reachable')
        self.assertEqual(result['rank_gram_lower'], dict(numerator=1, denominator=4))
        self.assertEqual(result['symmetric_command_radius'], dict(numerator=1, denominator=1))
        self.assertEqual(result['demand_norm_squared'], dict(numerator=3, denominator=100))
        self.assertEqual(result['guaranteed_norm_squared'], dict(numerator=1, denominator=4))
        self.assertFalse(result['vehicle_qualified'])
        self.assertFalse(result['slew_and_dynamics_qualified'])
        self.assertFalse(result['implemented_allocator_tracking_qualified'])

    def test_boundary_and_large_request_are_conservative(self):
        self.demand['axis_abs_bound'] = [0.5, 0, 0]
        self.assertEqual(authority.screen_box(self.report, self.demand)['status'],
                         'sufficient_static_incremental_box_reachable')
        self.demand['axis_abs_bound'] = [0.500001, 0, 0]
        self.assertEqual(authority.screen_box(self.report, self.demand)['status'],
                         'inconclusive_for_declared_demand_box')
        self.demand['axis_abs_bound'] = [0.4, 0.4, 0.4]
        self.assertEqual(authority.screen_box(self.report, self.demand)['status'],
                         'inconclusive_for_declared_demand_box')

    def test_narrow_actuator_box_and_rank_uncertainty_do_not_pass(self):
        report = copy.deepcopy(self.report)
        report['allocator_inputs']['maximum'][0] = 0.05
        self.assertEqual(authority.screen_box(report, self.demand)['status'],
                         'inconclusive_for_declared_demand_box')
        report = copy.deepcopy(self.report)
        report['effectiveness_abs_uncertainty'] = [[0.25] * 4 for _ in range(3)]
        self.assertEqual(authority.screen_box(report, self.demand)['status'],
                         'rank_not_proven_for_reported_intervals')

    def test_mock_measured_schema_remains_unqualified(self):
        self.profile['profile_kind'] = 'measured_single_condition'
        for canard in self.profile['canards']:
            canard['deflection_slope_uncertainty_rad_per_command'] = 0.001
            canard['moment_derivative_uncertainty_nm_per_rad'] = [0.01] * 3
        report = compiler.compile_profile(self.profile)
        report['input_sha256'] = 'a' * 64
        demand = dict(self.demand, profile_input_sha256='a' * 64,
                      purpose='candidate_vehicle_requirement_unreviewed')
        result = authority.screen_box(report, demand)
        self.assertEqual(result['status'], 'sufficient_static_incremental_box_reachable')
        self.assertFalse(result['vehicle_qualified'])
        self.assertFalse(result['physical_acceptance'])

    def test_bad_demand_is_rejected(self):
        for field, value in (('profile_input_sha256', '0' * 64),
                             ('purpose', 'candidate_vehicle_requirement_unreviewed'),
                             ('quantity', 'total_body_moment'),
                             ('axis_abs_bound', [-0.1, 0.1, 0.1]),
                             ('axis_abs_bound', [0, 0, 0]),
                             ('axis_abs_bound', [float('nan'), 0, 0])):
            altered = copy.deepcopy(self.demand)
            altered[field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                authority.screen_box(self.report, altered)

    def test_cli_rejects_compiled_tamper_and_writes_no_output(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            source = work / 'profile.json'
            source.write_bytes(EXAMPLE.read_bytes())
            compiled = work / 'compiled.json'
            compiled.write_text(json.dumps(self.report, indent=2) + '\n')
            output = work / 'authority.json'
            command = [sys.executable, str(Path(authority.__file__)), '--input', str(source),
                       '--compiled', str(compiled), '--evidence-root', str(ROOT),
                       '--demand', str(DEMAND), '--output', str(output)]
            self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertEqual(json.loads(output.read_text())['status'],
                             'sufficient_static_incremental_box_reachable')
            altered = copy.deepcopy(self.report)
            altered['allocator_inputs']['effectiveness'][0][0] = 0.3
            compiled.write_text(json.dumps(altered, indent=2) + '\n')
            command[-1] = str(work / 'rejected.json')
            rejected = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(rejected.returncode, 2)
            self.assertIn('compiled report differs', rejected.stderr)
            self.assertFalse(Path(command[-1]).exists())


if __name__ == '__main__':
    unittest.main()
