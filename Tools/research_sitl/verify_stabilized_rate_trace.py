#!/usr/bin/env python3
"""Run phase-controlled nose-up SITL with stock continuous gyro/rate ULog."""
import argparse
from pathlib import Path
import shutil

import verify_stabilized_mixed as mixed
import verify_stabilized_phased_jitter as phased
import verify_stabilized_sitl as base


TRACE_TOPICS = ('sensor_gyro', 'sensor_gyro_fifo', 'vehicle_imu',
                'vehicle_angular_velocity', 'vehicle_attitude', 'sensor_selection')


class TraceFixture(phased.PhaseFixture):
    REPEAT_ID = 0

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        storage = self.work / 'fs/etc/logging'
        storage.mkdir(parents=True)
        assert storage.resolve().is_relative_to(self.work.resolve())
        topics = storage / 'logger_topics.txt'
        topics.write_text(''.join(name + ' 0 0\n' for name in TRACE_TOPICS))
        original = self.script.read_text()
        assert original.count('mavlink boot_complete\n') == 1
        self.script.write_text(original.replace('mavlink boot_complete\n',
            'param set SDLOG_PROFILE 0\n'
            'logger start -m file -f -p vehicle_imu -b 512\n'
            'mavlink boot_complete\n'))
        self.result.update(rate_trace_source_sha256=base.digest(Path(__file__)),
                           rate_trace_topics=TRACE_TOPICS, trace_repeat_id=self.REPEAT_ID,
                           logger_topics_sha256=base.digest(topics),
                           generated_rc_sha256=base.digest(self.script))
        shutil.copy2(Path(__file__), self.work / Path(__file__).name)

    def close(self):
        if self.px4 is not None and self.px4.poll() is None:
            output, _ = self.client('logger', 'status', check=False)
            (self.work / 'logger-status.log').write_text(output)
        super().close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--repeat-id', type=int, choices=(0, 1, 2), required=True)
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    assert evidence.parent == Path('/var/tmp') and evidence.name.startswith('research-stabilized-sitl-')
    TraceFixture.REPEAT_ID = args.repeat_id
    mixed.run_case(evidence, True, 1, manual_period_s=phased.MANUAL_PERIOD_S,
                   imu_delay_us=phased.IMU_DELAY_US, fixture_type=TraceFixture)


if __name__ == '__main__':
    main()
