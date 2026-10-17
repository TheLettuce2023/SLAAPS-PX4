#!/usr/bin/env python3
"""Build an isolated POSIX uORB advertisement-revision/manual-owner variant."""
import difflib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

import verify_stabilized_sitl as base
from prepare_stabilized_generated_allocation import (
    ACCEPTED, ACCEPTED_MANIFEST_SHA256, ACCEPTED_PX4_SHA256, PLUGIN_SHA256,
    verify_receipt,
)

OWNER = Path('Tools/research_sitl/stabilized_manual/StabilizedManualSITL.cpp')
NODE_H = Path('platforms/common/uORB/uORBDeviceNode.hpp')
NODE_CPP = Path('platforms/common/uORB/uORBDeviceNode.cpp')
MODULE = Path('Tools/research_sitl/stabilized_manual/px4_external/src/research_stabilized_sitl/CMakeLists.txt')
SUPPORT = base.ROOT / 'Tools/research_sitl/publisher_epoch'


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Variant source anchor is not unique: ' + old[:90])
    return text.replace(old, new, 1)


def variant_sources(original):
    sources = dict(original)
    h = sources[NODE_H]
    h = replace_once(h, '\tvoid mark_as_advertised() { _advertised.store(true); }', '''
#if defined(RESEARCH_STABILIZED_PUBLISHER_EPOCH)
# if !defined(__PX4_POSIX)
#  error Advertisement revision instrumentation is private to POSIX SITL.
# endif
\tvoid mark_as_advertised()
\t{
\t\tATOMIC_ENTER;
\t\tresearch_advance_revision();
\t\t_advertised.store(true);
\t\tATOMIC_LEAVE;
\t}

\tunsigned research_advertisement_revision()
\t{
\t\tATOMIC_ENTER;
\t\tconst unsigned revision = _research_revision;
\t\tATOMIC_LEAVE;
\t\treturn revision;
\t}

\t// Queue-one selected state only. Payload and lifecycle use the same lock.
\tbool research_copy_revision(void *dst, unsigned &generation, unsigned &revision)
\t{
\t\trevision = 0;

\t\tif (dst == nullptr || _meta->o_queue != 1) { return false; }

\t\tATOMIC_ENTER;

\t\tif (!_advertised.load() || !_data_valid || _data == nullptr || _research_revision == 0
\t\t    || _research_data_revision != _research_revision) {
\t\t\tATOMIC_LEAVE;
\t\t\treturn false;
\t\t}

\t\tmemcpy(dst, _data, _meta->o_size);
\t\tgeneration = _generation.load();
\t\trevision = _research_data_revision;
\t\tATOMIC_LEAVE;
\t\treturn true;
\t}
#else
\tvoid mark_as_advertised() { _advertised.store(true); }
#endif'''.lstrip('\n'))
    h = replace_once(h, '\tpx4::atomic<unsigned>  _generation{0};  /**< object generation count */', '''
\tpx4::atomic<unsigned>  _generation{0};  /**< object generation count */
#if defined(RESEARCH_STABILIZED_PUBLISHER_EPOCH)
\t// Protected by the node lock. Zero is a permanent exhausted/invalid token.
\tunsigned _research_revision{1};
\tunsigned _research_data_revision{};
\tvoid research_advance_revision() { if (_research_revision != 0) { ++_research_revision; } }
#endif'''.lstrip('\n'))
    sources[NODE_H] = h
    cpp = sources[NODE_CPP]
    cpp = replace_once(cpp, '\t// callbacks\n', '''#if defined(RESEARCH_STABILIZED_PUBLISHER_EPOCH)
\t_research_data_revision = _research_revision;
#endif

\t// callbacks
''')
    cpp = replace_once(cpp, '\tdevnode->_advertised.store(false);', '''#if defined(RESEARCH_STABILIZED_PUBLISHER_EPOCH)
\tdevnode->lock();
\tdevnode->research_advance_revision();
\tdevnode->_advertised.store(false);
\tdevnode->unlock();
#else
\tdevnode->_advertised.store(false);
#endif''')
    sources[NODE_CPP] = cpp
    owner = sources[OWNER]
    owner = replace_once(owner, '#include <uORB/Subscription.hpp>', '#include <uORB/Subscription.hpp>\n#include "PublisherRevisionSubscription.hpp"')
    owner = replace_once(owner, 'namespace fixture = research_stabilized_fixture;',
                         'namespace fixture = research_stabilized_fixture;\nnamespace epoch = research_stabilized_epoch;')
    for name in ('attitude', 'rates', 'estimator', 'flags'):
        owner = replace_once(owner, 'uORB::Subscription _' + name + '{',
                             'epoch::PublisherRevisionSubscription _' + name + '{')
    owner = replace_once(owner, '\tbool _revoked{};', '''\tbool _revoked{};
\tepoch::PublisherRevisionBinding _epoch_binding{};
\tunsigned _revisions[epoch::PublisherRevisionBinding::kTopicCount] {};
\tuint32_t _enabled_epoch{};
\tpx4::atomic<int> _publisher_cycle{0};

\ttemplate<typename Message>
\tbool cyclePublisher(epoch::PublisherRevisionSubscription &subscription, int topic)
\t{
\t\tMessage message{};
\t\tunsigned before{}, after{};
\t\tconst bool success = subscription.cycleAdvertisement(message, before, after);
\t\tPX4_INFO("publisher_cycle topic=%d before=%u after=%u publication=%llu last_control=%llu success=%d",
\t\t\t topic, before, after, (unsigned long long)message.timestamp, (unsigned long long)_last_cycle_time, success);
\t\tevent(hrt_absolute_time(), "publisher_cycle", success ? Status::Ok : Status::InvalidState);
\t\treturn success;
\t}''')
    owner = replace_once(owner, '\tif (_revoked) { return; }', '''\tif (_revoked) { return; }

\t// Deterministic synthetic node-lifecycle injection between owner snapshots.
\tconst int publisher_cycle = _publisher_cycle.fetch_and(0);
\tbool injected = true;

\tswitch (publisher_cycle) {
\tcase 1: injected = cyclePublisher<vehicle_attitude_s>(_attitude, publisher_cycle); break;
\tcase 2: injected = cyclePublisher<vehicle_angular_velocity_s>(_rates, publisher_cycle); break;
\tcase 3: injected = cyclePublisher<estimator_status_s>(_estimator, publisher_cycle); break;
\tcase 4: injected = cyclePublisher<estimator_status_flags_s>(_flags, publisher_cycle); break;
\tdefault: break;
\t}

\tif (!injected) { revoke(hrt_absolute_time(), Status::InvalidState); return; }''')
    owner = replace_once(owner, '''\tconst bool have = _manual.copy(&manual_message) && _attitude.copy(&attitude)
\t\t\t  && _rates.copy(&rates) && _selection.copy(&selection)
\t\t\t  && _estimator.copy(&estimator) && _flags.copy(&flags) && _armed.copy(&armed);''', '''\tconst bool have = _manual.copy(&manual_message) && _attitude.copyRevision(&attitude, _revisions[0])
\t\t\t  && _rates.copyRevision(&rates, _revisions[1]) && _selection.copy(&selection)
\t\t\t  && _estimator.copyRevision(&estimator, _revisions[2])
\t\t\t  && _flags.copyRevision(&flags, _revisions[3]) && _armed.copy(&armed);
\tconst bool revisions_stable = have && _revisions[0] == _attitude.revision()
\t\t\t\t      && _revisions[1] == _rates.revision() && _revisions[2] == _estimator.revision()
\t\t\t\t      && _revisions[3] == _flags.revision();
\tconst uint32_t source_epoch = revisions_stable ? _epoch_binding.observe(_revisions) : 0;''')
    owner = replace_once(owner, 'const bool valid = have && profileValid()', 'const bool valid = have && source_epoch != 0 && profileValid()')
    owner = replace_once(owner, '\tsample.source_epoch = 1; // One explicit process/session, single EKF and bound IMU IDs below.',
                         '\tsample.source_epoch = source_epoch; // Bound four-topic advertisement revision tuple.')
    owner = replace_once(owner, '\tsample.valid = valid;', '''\tsample.valid = valid;

\t// Check before duplicate-clock suppression as lifecycle changes are not a
\t// control timestep. A frozen-time dispatch must still revoke changed state.
\tif (_session && source_epoch != _enabled_epoch) {
\t\trevoke(now, source_epoch == 0 ? Status::StateRejected : Status::StateChanged);
\t\treturn;
\t}''')
    owner = replace_once(owner, '\t\t_session = now;', '\t\t_session = now;\n\t\t_enabled_epoch = source_epoch;')
    owner = replace_once(owner, '\tfprintf(_log, ",%.9g\\n", (double)report.fixed_simulated_thrust_n);', '''\tfprintf(_log, ",%.9g,%u,%u,%u,%u,%u\\n", (double)report.fixed_simulated_thrust_n,
\t\tsample.source_epoch, _revisions[0], _revisions[1], _revisions[2], _revisions[3]);''')
    owner = replace_once(owner, ',u0,u1,u2,u3,thrust\\n', ',u0,u1,u2,u3,thrust,source_epoch,att_revision,rate_revision,est_revision,flags_revision\\n')
    owner = replace_once(owner, '''\tif (!instance || argc != 1) { return PX4_ERROR; }''', '''\tif (argc == 1 && strcmp(argv[0], "revision_selftest") == 0) { return epoch::publisherRevisionSelfTest(); }

\tif (!instance) { return PX4_ERROR; }

\tif (argc == 2 && strcmp(argv[0], "cycle_publisher") == 0) {
\t\tconst char *topics[] = {"attitude", "rate", "estimator", "flags"};
\t\tconst int state = instance->_state.load();

\t\tif (state != static_cast<int>(State::Hold) && state != static_cast<int>(State::Rate)) { return PX4_ERROR; }

\t\tfor (unsigned i = 0; i < epoch::PublisherRevisionBinding::kTopicCount; ++i) {
\t\t\tif (strcmp(argv[1], topics[i]) == 0) {
\t\t\t\tint expected = 0;
\t\t\t\treturn instance->_publisher_cycle.compare_exchange(&expected, static_cast<int>(i + 1)) ? PX4_OK : PX4_ERROR;
\t\t\t}
\t\t}

\t\treturn PX4_ERROR;
\t}

\tif (argc != 1) { return PX4_ERROR; }''')
    sources[OWNER] = owner
    sources[MODULE] = replace_once(sources[MODULE], 'SRCS ${fixture_root}/StabilizedManualSITL.cpp',
                                    'SRCS ${fixture_root}/StabilizedManualSITL.cpp ${fixture_root}/PublisherRevisionSelfTest.cpp')
    return sources


def build():
    verify_receipt(ACCEPTED, 'campaign-manifest.json', ACCEPTED_MANIFEST_SHA256)
    if base.digest(ACCEPTED / 'sitl-lockstep-build/bin/px4') != ACCEPTED_PX4_SHA256:
        raise RuntimeError('Accepted PX4 changed')
    if base.digest(ACCEPTED / 'plugin-build/libresearch_rotational_fixture.so') != PLUGIN_SHA256:
        raise RuntimeError('Accepted plugin changed')
    graph = json.loads((base.ROOT / 'docs/RESEARCH_CODE_GRAPH.json').read_text())
    checkpoint = {}
    for node in graph['nodes']:
        if 'sha256' in node:
            if base.digest(base.ROOT / node['path']) != node['sha256']:
                raise RuntimeError('Stale graph: ' + node['path'])
            checkpoint[node['path']] = node['sha256']
    paths = (OWNER, NODE_H, NODE_CPP, MODULE)
    originals = {}
    for path in paths:
        if base.digest(base.ROOT / path) != base.digest(ACCEPTED / 'px4-source' / path):
            raise RuntimeError('Accepted source mismatch: ' + str(path))
        originals[path] = (base.ROOT / path).read_text()
    modified = variant_sources(originals)
    out = Path(tempfile.mkdtemp(prefix='research-stabilized-sitl-', dir='/var/tmp'))
    print('Publisher revision evidence:', out, flush=True)
    (out / 'checkpoint.json').write_text(json.dumps(checkpoint, indent=2) + '\n')
    source = out / 'px4-source-publisher-epoch'
    base.run(['cp', '-a', '--reflink=auto', str(ACCEPTED / 'px4-source'), str(source)], out, 'source-copy.log')
    for path, text in modified.items():
        (source / path).write_text(text)
    support_files = {}
    for path in SUPPORT.iterdir():
        if path.is_file() and path.suffix in ('.hpp', '.cpp'):
            destination = source / base.FIXTURE.relative_to(base.ROOT) / path.name
            shutil.copy2(path, destination)
            support_files[str(path.relative_to(base.ROOT))] = base.digest(path)
    changed_cpp = [source / path for path in paths if path.suffix in ('.hpp', '.cpp')]
    changed_cpp += [source / base.FIXTURE.relative_to(base.ROOT) / Path(path).name for path in support_files]
    base.run(['bash', str(source / 'Tools/astyle/fix_code_style.sh'), *map(str, changed_cpp)], out, 'variant-format.log')
    patch = ''.join(''.join(difflib.unified_diff(originals[path].splitlines(True),
                          (source / path).read_text().splitlines(True),
                          fromfile='a/' + str(path), tofile='b/' + str(path))) for path in paths)
    (out / 'publisher-epoch.patch').write_text(patch)
    baseline = out / 'px4-source'
    for path in base.FIXTURE.rglob('*'):
        if path.is_file():
            destination = baseline / path.relative_to(base.ROOT)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, destination)
    native = out / 'sitl-lockstep-build'
    external = source / base.FIXTURE.relative_to(base.ROOT) / 'px4_external'
    base.run(['cmake', '-S', str(source), '-B', str(native), '-G', 'Ninja', '-DCONFIG=px4_sitl_default',
              '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-DCMAKE_CXX_FLAGS=-DRESEARCH_STABILIZED_PUBLISHER_EPOCH=1',
              '-DEXTERNAL_MODULES_LOCATION=' + str(external)], out, 'sitl-configure.log')
    base.run(['cmake', '--build', str(native), '--target', 'mavlink_c_generate', '-j', '4'], out, 'sitl-mavlink.log')
    base.run(['cmake', '--build', str(native), '-j', '4'], out, 'sitl-build.log')
    plugin = out / 'plugin-build'
    plugin.mkdir()
    shutil.copy2(ACCEPTED / 'plugin-build/libresearch_rotational_fixture.so', plugin / 'libresearch_rotational_fixture.so')
    record = dict(scope='private_posix_advertisement_revision_manual_owner_variant',
                  parent_manifest_sha256=ACCEPTED_MANIFEST_SHA256, parent_px4_sha256=ACCEPTED_PX4_SHA256,
                  variant_px4_sha256=base.digest(native / 'bin/px4'), plugin_sha256=PLUGIN_SHA256,
                  original_sources={str(path): base.digest(base.ROOT / path) for path in paths},
                  patched_sources={str(path): base.digest(source / path) for path in paths},
                  support_sources=support_files, preparation_anchors=len(checkpoint),
                  production_source_changed=False, vehicle_qualified=False, accepted_campaign_expansion=False)
    (out / 'publisher-epoch-origin.json').write_text(json.dumps(record, indent=2) + '\n')
    shutil.copy2(Path(__file__), out / Path(__file__).name)
    print(json.dumps(dict(evidence=str(out), px4_sha256=record['variant_px4_sha256'], anchors=len(checkpoint)), indent=2), flush=True)


if __name__ == '__main__':
    build()
