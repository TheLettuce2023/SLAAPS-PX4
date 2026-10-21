#!/usr/bin/env python3
"""Generate a private transport variant while retaining the exact physical model."""
import sys
from pathlib import Path
source, target = map(Path, sys.argv[1:])
s = source.read_text()
def replace(old, new):
    global s
    assert s.count(old) == 1, old
    s = s.replace(old, new)
replace('#include "FixtureConstants.hpp"', '#include "FixtureConstants.hpp"\n#include "GazeboPlantTransport.hpp"')
replace('_log << std::setprecision(17);', '_transport.configure(sdf->Get<std::string>("session_namespace"));\n\t\t_log << std::setprecision(17);')
replace('if (!_node.Subscribe("/model/slaaps_fixture/stabilized_command", &RotationalFixture::command, this)\n\t\t    || !_node.Subscribe', 'if (!_node.Subscribe')
a=s.index('\t\t\tif (_invalid)');b=s.index('\n\t\t\tinjected = _impulse;',a)
s=s[:a]+s[b:]
replace('\t\tgz::math::Vector3d injected;', '\t\tif (!_transport.prepare(now)) { _transport.revoke(); return; }\n\t\tgz::math::Vector3d injected;')
assert s.count('if (!pose || !velocity) { return; }') == 2
# First occurrence belongs to PreUpdate; PostUpdate has no pending write.
s=s.replace('if (!pose || !velocity) { return; }','if (!pose || !velocity) { _transport.complete(now, false); return; }',1)
replace('_link.AddWorldWrench(ecm, pose->Rot().RotateVector(force_flu), pose->Rot().RotateVector(torque_flu));', '_link.AddWorldWrench(ecm, pose->Rot().RotateVector(force_flu), pose->Rot().RotateVector(torque_flu));\n\t\t_transport.complete(now, true);')
a=s.index('\tstatic bool integer(double value)');b=s.index('\tvoid impulse(',a);s=s[:a]+s[b:]
a=s.index('\tresearch_actuator_command::BenchOutputLease _lease{};');b=s.index('\tstd::array<double, 4> _surface{};',a);s=s[:a]+'\tresearch_stabilized_session_fixture::GazeboPlantTransport _transport{};\n'+s[b:]
s=s.replace('_lease.revoke()', '_transport.revoke()').replace('_lease.faulted()', '_transport.faulted()').replace('_lease.value(now, i)', '_transport.value(i)').replace('!_transport.faulted() && finite_commands', '_transport.enabled() && finite_commands')
s=s.replace('_accepted.timestamp', '_transport.accepted().frame.timestamp_us').replace('_accepted.session', '_transport.accepted().frame.session').replace('_accepted.lease_us', '_transport.accepted().lease_us')
s=s.replace('\tbool _pending{};\n','').replace('\tbool _invalid{};\n','')
assert '_lease' not in s and '_pending' not in s
s=s.replace('class RotationalFixture final', 'class RotationalSessionFixture final').replace('~RotationalFixture()', '~RotationalSessionFixture()').replace('&RotationalFixture::','&RotationalSessionFixture::').replace('research_stabilized_fixture::RotationalFixture','research_stabilized_fixture::RotationalSessionFixture')
target.write_text(s)
