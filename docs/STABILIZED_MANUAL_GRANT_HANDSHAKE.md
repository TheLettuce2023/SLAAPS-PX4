# Stabilized manual: coherent grant handshake

2026-10-20, Codex. Shared `StabilizedGrantRequest`/`StabilizedGrantResponse` uORB
schemas and `GrantHandshake.hpp` now define a coherent startup exchange. The
native scheduled fixture uses them in place of separate grant/source atomics.
The production observer remains output-disabled; this is verified transport and
validation for the future production supervisor, not a deployed runtime owner.

## Request, response and startup

One request carries its original monotonic session token, exclusive deadline,
immutable configuration identity, and expected command/acknowledgment publisher
lifetimes. A consumer bound explicitly to a supervisor lifetime copies the
payload and revision atomically. It checks the current configuration and source
lifetimes before invoking the real output adapter. The adapter still requires
confirmed disarm, current permission and an unexpired request.

The consumer echoes the complete request identity and copied supervisor revision
in its acceptance response. Controller startup copies that response with its
publisher metadata and checks every echoed field, decision time, deadline and
current request/response/command/acknowledgment lifetime. After draining retained
output acknowledgments, it re-copies and revalidates the response immediately
before beginning the controller session. A rejected startup leaves a running
session intact.

Explicit revocation invalidates the published acceptance response. Consumer
faults invalidate it after dispatch. Supervisor lifetime loss also removes output
permission at the driver's pre/post-write checks, latches revocation and requires
explicit reauthorization. A replacement supervisor is not trusted automatically.
Response-publisher binding governs startup confirmation; the already-running
session continues under its independently bound output acknowledgments and lease.
Publisher revisions establish lifetime continuity, not publisher authentication.
Configuration identifiers must refer to the same immutable configuration at both
ends; the fixture's identifier 1 and two-second request window are synthetic.

## Verification

Thirty-seven scenarios repeated twice pass **8,822 assertions and 2,258 output
acknowledgments**. Added cases exercise actual supervisor withdrawal, supervisor
restart with retained requests, and response-publisher restart with a retained
acceptance. Every case rejects expired requests, wrong configuration and wrong
acknowledgment bindings before starting. Startup probes reject altered response
fields, past/future decision times, expiry and changed lifetime metadata.
The final ELF also passes 14,206 observer checks. Debug/Release/ASan-UBSan each
retain 126,287 portable checks; these host counts do not include the new native
handshake assertions.

Native evidence: `/var/tmp/research-stabilized-output-native-handshake-20261020`.
All 1,249 sealed files rehash, including preserved earlier attempts. The accepted
74-run list binds only the final ELF. Manifest SHA-256:
`e5ba3f585855d96e5cdb2bfc87041c00058b981141b2e98605340f4ce837bd6a`.
Final ELF: `ac33fcc7a1b757585a3445cee65f2f3b4e763cc93ec557093e9d6d43a9ff493d`.
The first supervisor-withdrawal run had zero native assertion failures but failed
the runner's existing minimum of 20 acknowledgments because it intentionally
forbids successor startup. The fixture now requires 20 initial acknowledgments
before fault injection; the runner threshold was not lowered. Final startup
ordering was subsequently rebuilt and the complete suite rerun.

ARM evidence: `build/stabilized_grant_handshake/20261020`.
All 33 receipt files rehash and bind the current validator and actual native-built
message headers. Receipt SHA-256:
`f8cc662e765c849748deb2d394899d4e07290d569f107792f0540ea180f3b60a`.
The validator wrapper compiles to 324 text bytes. ARM structure sizes are 56 bytes
for the expected handshake, 40 for the request and 48 for the response. Probe BSS
contains size-measurement arrays, not production runtime allocation. This is an
unlinked compiler check; no complete firmware image was linked or hardware operated.

## Remaining boundary

Connect this exchange to deliberate production mode requests, immutable profile
selection, authoritative selected-state health, independent producer/consumer
scheduling and physical driver attachment. Review the full linked-image resource
budget, including the newly registered message metadata. The native consumer's
synthetic permission/arming sources are not vehicle health policy. Reauthorization
and publisher authentication remain supervisor responsibilities.

The [qualified synthetic console](STABILIZED_MANUAL_SESSION_REQUALIFICATION.md)
remains pinned to its tested pair; this transport is not yet part of its dynamic
fixture owner. CAD/plant/servo measurements and bench timing gates remain pending.
Stock and bench baselines, H10 and acceleration gates retain their prior scope.
