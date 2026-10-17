# O1: cooperative SITL supervisor broker core

Status: O1 offline implementation verified and accepted, 2026-09-19. User selected option 1:
cooperative trusted-local processes, not hostile same-user isolation. This
slice is host-only C++ state-machine code and fixtures, not an interactive
console, scheduler, authentication provider, MAVLink link, or PX4 module.

## Ownership and scope

New files: Tools/research_sitl/supervisor/SupervisorBroker.hpp/.cpp,
SupervisorBrokerTest.cpp, SupervisorBrokerMatrixTest.cpp and README.md. Registration occurs only inside
BUILD_TESTING of src/lib/research_acceleration/CMakeLists.txt as a separate
host library/test. Existing adapter/guard/status/codec source remains unchanged.
Root owns source/build/docs; a worker owns independent test/oracle paths after
API acceptance; independent review is read-only.

SupervisorBroker owns attachment history, one pending confirmation, local
ticket sequence and committed supervisor-event floor. A trusted injected
SupervisorOwner owns the actual WireAdapter, continuous observation, ingress
evidence and the one serialization domain shared with this broker. The broker
never receives or manufactures a session packet/parser verdict. Owner submit
is synchronous, bounded and nonblocking; it performs final view validation
and one existing adapter administration call, or returns NotSubmitted with
no administration/event side effect. Unknown completion quarantines authority.
A fake owner fixture calls the REAL unchanged adapter, not a duplicate guard.

UI role may call prepare(intent, session), confirm(ticket), and detach.
attach(operator_instance) is a TRUSTED harness attachment seam, not UI identity
or authentication. Owner interfaces and full audit results are not UI APIs.
C++ accessibility under one trusted process is a role convention, not a
security boundary. No code creates OS users, keys, ports, or files.

## Concrete types and interfaces

All identities/counters/time use uint64_t. Public namespace research_sitl;
PX4 contract types retain research_acceleration namespace.

- Intent: Establish, Revoke, ClearLatch.
- BrokerConfig: maximum_confirmation_age_us, mandatory nonzero fixture setting.
  No deployment default. Exact maximum age is inclusive; checked subtraction,
  never adding a deadline near UINT64_MAX.
- OwnerView: ready, terminal, controller_instance_id, state_generation,
  transition_epoch, clock_mapping_epoch, validator_time_us,
  has_session_history, last_session (TrustedSession),
  has_supervisor_floor, supervisor_floor.
- Submission: intent, requested_session, expected_view, SupervisorEvent event.
  Broker-internal trusted-owner boundary only.
- OwnerOutcome: NotSubmitted, Completed, Unknown.
- SupervisorOwner: virtual bool snapshot(OwnerView &) noexcept;
  virtual OwnerOutcome submit(const Submission &, AdminResult &) noexcept.
- ConsoleResult: disposition (NotSubmitted/Submitted/Quarantined), code,
  ticket, has_adapter_code, adapter_code, event_consumed.
  No AdminResult, epoch, event ID, parser evidence or trusted state is exposed.
- BrokerCode: Ready, Submitted, NotConfigured, NotAttached, Busy, InvalidIntent,
  InvalidTicket, OwnerUnavailable, OwnerNotReady, ContextChanged, Expired,
  CounterExhausted, AuthorityDiscontinuity, OwnerDeclined, OutcomeUnknown,
  AdapterUnconsumed, InvalidOwnerResult, Quarantined.
- SupervisorBroker(config, owner): noncopyable/nonmovable, no reset API.
  bool attach(uint64_t operator_instance) noexcept;
  bool detach() noexcept;
  ConsoleResult prepare(Intent, const TrustedSession &) noexcept;
  ConsoleResult confirm(uint64_t ticket) noexcept;
  bool quarantined() const noexcept.
  Owner diagnostics may read a bool copyLastAdminResult(AdminResult &) const
  noexcept seam; failure leaves output unchanged. UI never gets this capability.

## Binding, freshness, and operation rules

Construct disconnected; zero maximum confirmation age config disables actions.
Attachment identity must be nonzero and strictly increase across attachments.
Repeated attach while attached fails without changing the current attachment.
Detach cancels pending confirmation but never calls adapter/revokes/clears.
Counters, controller binding, diagnostic result and quarantine survive detach.
There is no object reconstruction/resume API for an existing authority stream.

prepare checks configured/quarantine/attachment/reentry, valid intent/session
(nonzero tuple for establish/revoke; all-zero tuple for clear), no pending
confirmation, and a current ready nonterminal owner view. Missing required
identity/generation/epoch/map epoch or inconsistent history/floor is not ready.
First successful prepare binds controller identity and requires an unused
supervisor stream. Later view controller/floor must match broker's bound
controller/committed floor exactly; a competing stream or restart quarantines.
Owner time must never regress across successful snapshots; regression
quarantines this attachment without inventing an adapter latch.
After checks reserve a monotonically increasing opaque ticket; zero unused.
One slot only: Busy never overwrites the pending confirmation. UINT64_MAX is
usable once, then the next reservation quarantines without wrapping.

confirm with wrong/zero ticket does not cancel the correct pending one.
A matching ticket is consumed BEFORE calling any owner seam, whether the
subsequent attempt succeeds or fails. Snapshot failure/not-ready/stale view/
expiry cannot replay that ticket. Exact context equality compares controller,
generation, transition epoch, map epoch, session-history tuple and supervisor
floor (time may advance). A change invalidates confirmation; it never rebases
the user's old consent. Current time minus preparation time must be <= config.
The UI does not provide this time. Unavailable/invalid context never reserves
an event or calls submit; independent owner observation still runs.

Immediately before submit, form event = committed floor + 1 (first 1).
Clear uses retained session history for event binding, not user session.
Establish/revoke use the exact selected tuple. Bind controller/generation/
transition epoch from current view, already observed by owner. This core
requires an initialized coherent view; first-observation epoch projection is
deferred to the owner's observation path rather than duplicated in the broker.
The owner's final atomic check must reject any changed view, including time,
between snapshot and submit; no UI I/O/confirmation occurs under serialization.
All callbacks and broker methods run in that same owner thread. A reentrant
call returns Busy (attach/detach false), with no mutation or second dispatch.

NotSubmitted means no adapter call/event consumption, drops confirmation and
does not advance event floor. A later fresh confirmation may reuse the
unsubmitted candidate number, never the old ticket/intent.
Completed supplies actual AdminResult. Accept only bounded nonzero reports,
matching consumed supervisor ID, exact packet correlation for establish/revoke
(or zero packet correlation for clear), coherent session supervisor floor and
nonterminal initialized adapter snapshot. Every consumed rejection advances
the floor too; Completed does NOT mean AdminCode::Completed.

The coherent view has already been observed: a consumed success returns two
reports (administrative barrier then operation), advances transition epoch
exactly one, and has the selected action/session gate. A consumed rejection
returns one operation report and unchanged epoch. Reports are chronological,
carry matching controller/time evidence, and the final report agrees with
the result's code/correlation/snapshot sequence. Wire operation sequence zero
is valid. The trusted owner, not the UI/broker, checks the exact packet sequence
against its ingress receipt. A structural terminal with a valid unconsumed
report may be shown as AdapterUnconsumed; a zero-report or inconsistent result
is InvalidOwnerResult. Both quarantine without advancing the broker floor.
admission_closed is dynamic on rejection; it must be true on successful action.
Known session-gate values are checked, but the exact gate on a rejected action
remains owner-authoritative: OwnerView deliberately does not duplicate the
adapter gate or S1 state machine. Successful action gates are checked exactly.
Nonterminal results cannot use the reserved UINT64_MAX report sequence.
A trustworthy result with event_consumed=false quarantines; no guess/retry.
Unknown outcome or impossible/mismatched result quarantines without exposing
a default/forged adapter code. Full returned result is retained internally for
diagnostics; only validated results supply a UI adapter outcome.
After submit with event ID UINT64_MAX, no later event can be issued.
Quarantine does not stop owner polling and never reconstructs the guard.

## Test boundary and gate

Mandatory oracle: attachment identity/reset, invalid intents/config/views,
one-slot/duplicate/cancel semantics, exact context changes, age at boundary and
one over, time regression and UINT64_MAX arithmetic, stream/controller changes,
all three owner outcomes, malformed result correlation/floor/report counts,
consumed rejection, unconsumed quarantine, reentrant owner callback,
event/ticket exhaustion (test-peer seeding only), and unchanged failed copies.

Actual-adapter fixtures must establish/revoke/clear via genuine C0b methods,
prove no self-authorization from a packet or UI tuple, and keep periodic polls
running while detached/quarantined. Test packet lookup/rejection and actual
session/ACK/history changes separately from pure broker fault injection.
No test claims deployed authentication, real deadlines, concurrent scheduling,
interactive UI, live transport, controller behavior or flight feasibility.

Gate requires strict Debug, repeated Release, ASan/UBSan, unchanged old suites,
master source/oracle inspection and independent review before acceptance.
