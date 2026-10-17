#!/usr/bin/env python3
"""Run mixed-axis SITL maneuvers with a logged simulation-clock pilot schedule."""
import argparse
from pathlib import Path
import shutil
import time

import verify_stabilized_campaigns as campaigns
import verify_stabilized_mixed as mixed
import verify_stabilized_sitl as base


MANUAL_PERIOD_S = 0.08
IMU_DELAY_US = 80000
SEND_INTERVALS_US = (76000, 84000)  # Alternating target jitter of -/+4 ms.
FIRST_SEND_AFTER_ARM_US = 12000


class PhaseFixture(campaigns.Fixture):
    def __init__(self, *args, **kwargs):
        self.phase_enabled = False
        self.next_target_us = 0
        super().__init__(*args, **kwargs)
        self.send_trace = (self.work / 'manual-send.csv').open('w', buffering=1)
        self.send_trace.write('index,target_sim_time,observed_sim_time,pitch,roll,yaw\n')
        self.result.update(phased_source_sha256=base.digest(Path(__file__)),
                           phased_intervals_us=SEND_INTERVALS_US,
                           phase_first_send_offset_us=FIRST_SEND_AFTER_ARM_US,
                           phase_clock='flushed_250hz_raw_imu_delivery_log',
                           startup_sender_wall_period_s=MANUAL_PERIOD_S)
        shutil.copy2(Path(__file__), self.work / Path(__file__).name)

    def phase_now(self):
        path = self.work / 'truth.csv.imu.csv'
        if not path.exists():
            return 0
        with path.open('rb') as stream:
            stream.seek(max(0, path.stat().st_size - 1024))
            lines = stream.read().decode().splitlines()
        complete = [line.split(',') for line in lines if line.count(',') == 7]
        stamps = [int(fields[1]) for fields in complete
                  if fields[0].isdigit() and fields[1].isdigit()]
        return stamps[-1] if stamps else 0

    def send(self):
        next_wall = time.monotonic()
        index = 0
        try:
            while not self.stopped.is_set():
                if self.phase_enabled:
                    observed = self.phase_now()
                    if observed >= self.next_target_us:
                        pitch, roll, yaw = self.command
                        self.connection.mav.manual_control_send(1, pitch, roll, 500, yaw, 0)
                        self.send_trace.write(f'{index},{self.next_target_us},{observed},'
                                              f'{pitch},{roll},{yaw}\n')
                        self.next_target_us += SEND_INTERVALS_US[index % len(SEND_INTERVALS_US)]
                        index += 1
                elif self.sending and time.monotonic() >= next_wall:
                    pitch, roll, yaw = self.command
                    self.connection.mav.manual_control_send(1, pitch, roll, 500, yaw, 0)
                    next_wall = time.monotonic() + MANUAL_PERIOD_S
                self.stopped.wait(0.001)
        except Exception as error:
            (self.work / 'sender-error.txt').write_text(repr(error))

    def start(self):
        super().start()
        anchor = self.phase_now()
        assert anchor > 0, 'Raw IMU delivery clock did not start'
        self.next_target_us = anchor + FIRST_SEND_AFTER_ARM_US
        self.result['phase_anchor_us'] = anchor
        self.phase_enabled = True
        self.action('phase_schedule_start', str(self.next_target_us))

    def close(self):
        try:
            super().close()
        finally:
            self.send_trace.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--nose-up', action='store_true')
    parser.add_argument('--first-sign', type=int, choices=(-1, 1), default=1)
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    assert evidence.parent == Path('/var/tmp') and evidence.name.startswith('research-stabilized-sitl-')
    mixed.run_case(evidence, args.nose_up, args.first_sign, manual_period_s=MANUAL_PERIOD_S,
                   imu_delay_us=IMU_DELAY_US, fixture_type=PhaseFixture)


if __name__ == '__main__':
    main()
