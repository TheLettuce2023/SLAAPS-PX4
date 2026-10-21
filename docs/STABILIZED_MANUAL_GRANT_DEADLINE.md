# Stabilized manual: expiring ownership requests

2026-10-20, Codex. `ResearchStabilizedOutput::begin` now requires one
`ResearchOutputGrant` containing the request/session creation token, an exclusive
acceptance deadline and the expected command-publisher revision. The consumer
must receive this as a coherent out-of-band snapshot. Frames still cannot grant
ownership. The supervisor must not extend a deadline when retrying a request.

An unattached, active, pending or uncollected adapter still rejects a replacement
before changing its owner. After source binding and confirmed disarm checks, zero
or future request tokens and deadlines at/before creation are invalid. A request
at or past its deadline returns `Expired` without granting ownership. Once a grant
is accepted, command expiry continues to use the independent command lease; the
request deadline does not become an active-session lease.

The native fixture derives its two-second request window from the immutable
request token. Every scenario first queues an already expired request and checks
that no grant or command results, then issues a fresh monotonic token. Direct
adapter probes check future requests, malformed deadlines and exact-deadline
expiry. Existing competing-owner, source replacement and confirmed-disarm tests
remain. The two-second window is a synthetic fixture choice, not a vehicle policy.

## Verification

All 68 native runs pass 5,458 assertions and 1,439 acknowledgments. The same ELF
passes 14,158 observer checks. Debug, Release and ASan/UBSan each retain 126,287
portable checks. Evidence is `/var/tmp/research-stabilized-output-native-grant-20261020`;
all 547 manifest files rehash. Manifest SHA-256:
`c8d797f7401bb31719d7179adc0508cacb1f23ad120b34502a2e8d589c67a5e8`.

ARM stage: `build/stabilized_output_grant/20261020-final`. All 290 receipt files
rehash. Receipt SHA-256:
`77cbdb4321b1d65e4a0383a7dbabdc188c431819a58b1f296788ee8075f13775`.
Adapter text is 1,737 bytes, 56 more than the prior source-bound adapter; instance
size remains 848 bytes. Enabled mixer text remains 8,661 bytes. Disabled mixer
text is 8,233 bytes and its machine code/relocations match baseline exactly.
The first ARM attempt is preserved in `20261020`: the adapter compiled, but an
unrelated subscription probe lacked its overlay header. The final recipe tests
the changed adapter and mixer without that unchanged probe. No firmware image
was linked or hardware operated.

## Remaining integration

This implements consumer-side request expiry, not the production supervisor or
request transport. The supervisor must bind immutable configuration/session and
source metadata, deliver the request coherently, obtain a matching grant outcome,
and only then start the controller session. Authoritative health, independent
scheduling, physical driver attachment and full-image resource checks remain.
The dynamically qualified synthetic console remains on its preserved tested pair.
Vehicle measurement and bench timing gates are unchanged.
