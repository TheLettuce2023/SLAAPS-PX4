# C1 proposal: first output-disabled PX4/SITL attachment

2026-09-20. **Approved by the user's latest "Proceed".** C1H has accepted
bounded host evidence. C1 source/design review and implementation have started;
approval does not mean the attachment has passed verification. See
C1_PX4_ATTACHMENT_DESIGN.md for the bounded passive C1A first slice. C1A now
passes its fresh build/two-run verification in C1A_PX4_PROBE_VERIFICATION.md;
this does not complete the wider C1 attachment proposed here.

Requested authority: detailed source/design review, implementation and bounded
testing of the research interface against PX4 SITL, optionally using the stock
gz_rc_cessna Gazebo baseline only for simulation/estimator/state plumbing.
This is not custom-vehicle physics or acceleration-controlled flight.

Constraints:

- Existing WSL/tools and pinned repositories only; serial simulator builds.
  Establish explicit instance/build/cache/mount ownership before starting;
  stop for missing dependencies, not automatic package installation.
- Preserve the stock Cessna model/configuration and separate Gazebo repository.
  Use an isolated opt-in test configuration and owned local endpoints.
- Keep the vehicle disarmed. No physical hardware/TELEM, arming, Offboard
  activation, actuator enablement, torque/thrust/servo publication or research
  flight controller release. A received/validated-disarmed request is not an
  eligible flight demand.
- Review the actual PX4 receive/state/clock/authority ownership before code.
  The host's shared clock proof does not transfer to another process or PX4
  lockstep/simulation time automatically. Unsupported maps remain invalid.
- Retain one admission owner, independent cooperative operator consent, signed
  private test identities, fresh ephemeral keys, exact status provenance and
  no simulator truth in operational controller inputs.
- Predeclare attachment tests for malformed/foreign/replayed/stale traffic,
  unknown clock/state, restart, disconnect and competing ownership. Preserve
  incomplete/failing evidence. No claim of tracking or physical fallback.
- No commit/push/PR, install, flashing, powered test or hardware authority.

After approval, master design review must select the concrete source hook,
isolated build/startup approach, real state/time provenance and test procedures
before implementation. New authority or unresolved operational requirements
remain explicit gates rather than guessed defaults.

No geometry measurement is required for this output-disabled interface work.
Validated mass/inertia, aerodynamic/actuator evidence and control architecture
are still required before the custom Gazebo vehicle can run closed-loop.
