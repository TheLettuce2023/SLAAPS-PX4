#!/usr/bin/env python3
"""Run one phase-controlled synthetic case with direct owner gyro/rate epochs."""
import argparse
import json
from pathlib import Path
import shutil

import verify_stabilized_mixed as mixed
import verify_stabilized_phased_jitter as phased
import verify_stabilized_sitl as base


VARIANT_SOURCE = Path('Tools/research_sitl/stabilized_manual/StabilizedManualSITL.cpp')
PATCH = base.ROOT / 'Tools/research_sitl/gyro_epoch_probe.patch'


class EpochFixture(phased.PhaseFixture):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        record_path = self.evidence / 'gyro-epoch-origin.json'
        record = json.loads(record_path.read_text())
        assert record['scope'] == 'private_synthetic_gyro_epoch_diagnostic_variant'
        assert base.digest(PATCH) == record['patch_sha256']
        assert base.digest(self.evidence / 'px4-source-gyro-epoch' / VARIANT_SOURCE) == \
            record['patched_owner_sha256']
        assert base.digest(base.ROOT / VARIANT_SOURCE) == record['original_owner_sha256']
        assert base.digest(self.evidence / 'sitl-lockstep-build/bin/px4') == \
            record['diagnostic_px4_sha256']
        assert self.result['px4_sha256'] == record['diagnostic_px4_sha256']
        assert self.result['plugin_sha256'] == record['relay_plugin_sha256']
        self.result.update(gyro_epoch_runner_sha256=base.digest(Path(__file__)),
                           gyro_epoch_origin_sha256=base.digest(record_path),
                           gyro_epoch_patch_sha256=record['patch_sha256'],
                           gyro_epoch_modified_owner_sha256=record['patched_owner_sha256'],
                           gyro_epoch_queue_drain=True,
                           gyro_epoch_diagnostic_variant=True)
        shutil.copy2(Path(__file__), self.work / Path(__file__).name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    assert evidence.parent == Path('/var/tmp') and evidence.name.startswith('research-stabilized-sitl-')
    mixed.run_case(evidence, True, 1, manual_period_s=phased.MANUAL_PERIOD_S,
                   imu_delay_us=phased.IMU_DELAY_US, fixture_type=EpochFixture)


if __name__ == '__main__':
    main()
