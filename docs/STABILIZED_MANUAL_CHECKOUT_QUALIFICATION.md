# Fresh checkout qualification and default console

2026-10-06, Codex. The default isolated SLAAPS manual console now uses the
fresh checkout-built PX4/plugin pair. All **31 declared cases were run freshly**
and pass over **68,813 control cycles**. The actual stdin-driven console also
passes. The earlier revision-aware package and its default profile are preserved.
Vehicle-supported SM5 allocation and tuning still await measured inputs.

## Build, qualify and run

The [checkout build recipe](STABILIZED_MANUAL_CHECKOUT_BUILD.md) produces a
source-bound pair and a focused `checkout-build-manifest.json`. Use the printed
build directory and receipt SHA in the full qualifier. The paths below name
the completed packages; a new invocation prints a new qualification directory.

```powershell
wsl -d Ubuntu-24.04 -- python3 '/mnt/c/Projects/SNC SENIOR DESIGN/FC Software + SITL/Tools/research_sitl/qualify_stabilized_checkout.py' --from-build /var/tmp/research-stabilized-sitl-q90g9o8f --build-receipt-sha256 95e129cbcad336fce318bdb47817e9a8e3ea96a464757997be0606e4e3ea493b
wsl -d Ubuntu-24.04 -- python3 '/mnt/c/Projects/SNC SENIOR DESIGN/FC Software + SITL/Tools/research_sitl/qualify_stabilized_checkout.py' --evidence /var/tmp/research-stabilized-sitl-0crhkf9n
wsl -d Ubuntu-24.04 -- python3 '/mnt/c/Projects/SNC SENIOR DESIGN/FC Software + SITL/Tools/research_sitl/qualify_stabilized_checkout.py' --evidence /var/tmp/research-stabilized-sitl-0crhkf9n --seal
wsl -d Ubuntu-24.04 -- python3 '/mnt/c/Projects/SNC SENIOR DESIGN/FC Software + SITL/Tools/research_sitl/create_stabilized_manual_profile.py' --qualification /var/tmp/research-stabilized-sitl-0crhkf9n --receipt-sha256 899a1ddd82e3226f715e74be1b4a90fa30c714b8759f276978a8cb904f3bfc3a
wsl -d Ubuntu-24.04 -- python3 '/mnt/c/Projects/SNC SENIOR DESIGN/FC Software + SITL/Tools/research_sitl/verify_stabilized_manual_console.py' --profile /var/tmp/research-stabilized-sitl-0crhkf9n/qualified-console-profile.json
```

The qualifier supports `--phase dispatch|coupled|nominal|failures|lifecycle`
for sequential continuation. Complete all phases before sealing. Keep a sealed
package immutable and prepare a new directory for a new campaign.
Profile creation verifies the exact receipt, artifact hashes and current
fixture inputs, and refuses to overwrite a different profile. It grants no
physical authority. The example profile is byte-identical to the promoted
`Tools/research_sitl/stabilized_manual_default.json`.

Run the current default directly:

```powershell
wsl -d Ubuntu-24.04 -- python3 '/mnt/c/Projects/SNC SENIOR DESIGN/FC Software + SITL/Tools/research_sitl/stabilized_manual_console.py'
```

Commands and body-axis semantics remain in
[the isolated-mode instructions](STABILIZED_MANUAL_ISOLATED_MODE.md). The
workflow builds from current indexed files, tracked changes and untracked
research inputs. It does not require the older accepted source trees or binaries.

## Full fresh evidence

Qualification: `/var/tmp/research-stabilized-sitl-0crhkf9n`.
Receipt `revision-qualification-manifest.json`, SHA-256
`899a1ddd82e3226f715e74be1b4a90fa30c714b8759f276978a8cb904f3bfc3a`.
ELF `67a6bab516dcdf0b9d7df76d548b3931f6c03fcb93b7814242e966690a6a0193`.
Plugin `a5addecdf9ed5378ffba95a1dd0dde62df44d08240c4a26541393edb577fb789`.

| Coverage | Fresh cases | Audited control cycles |
| --- | ---: | ---: |
| Frozen-clock duplicate dispatch | 1 | 353 |
| Full-axis commands/reversal/release/impulse, both poses/orders | 4 | 25,889 |
| Individual-axis maneuvers and six fault/recovery cases | 18 | 38,630 |
| Actual EKF2 lifecycle/reset/profile barriers, both poses | 8 | 3,941 |
| Total | 31 | 68,813 |

All original numerical limits and component audits remain. Worst coupled
physical tracking is 0.028645396 rad/s against 0.08; final physical hold error
is 0.003841353 rad against 0.02; peak is 0.496934980 rad against 0.5. There are
zero directional antiwindup violations. Every published session retains its
bound revision tuple. All eight state barriers stop the consumer in 4 ms and
require explicit fresh-session recovery. Real EKF2 stop/start advances
`(3,3,3,3)` to `(6,3,6,6)`; reset/profile cases keep their tuple and still revoke.

The qualification revalidates all 57,547 staged build inputs, binds the sealed
387-artifact build proof, and captures 441 preparation anchors and affected
compiled source snapshots. Its 1,475 artifacts independently rehash from
Windows with zero failures. Fresh lifecycle raw data is under `fresh-lifecycle`,
separate from the other component receipts. The build's 14 native uORB checks
remain part of its retained proof, outside the 31-case cycle count.

## Actual console evidence and capture correction

Passing check: `/var/tmp/research-stabilized-console-check-__heq56u`.
Receipt `console-manifest.json`, SHA-256
`c65771974a65c8601a520d802e038adab825e82d41227a14a7d9ff1e5207a76c`.
Five check files and 41 session files independently rehash clean.
Session `/var/tmp/research-stabilized-manual-session-inxgx9ev` advances from
5,444,000 to 6,648,000. Both native processes exit zero; metadata records
`closed=true`, `ready=false`, and final process checks find neither process live.

The initial check failed because the verifier chose the latest centered hold
row and required its evolving attitude estimate to equal the captured reference.
The actual first transition row captured exactly: error zero. Sixteen simulated
milliseconds later the estimate differed by 0.000208038 rad while the reference
remained fixed. That failed check, its original verifier, closed session and
diagnosis are preserved in `research-stabilized-console-check-avatikwn`, receipt
`4c2a6cb7374812ea37fb005d23495ea8a3c3325ca926cfc9c8f123ec07ddfba2`.

The corrected verifier selects the actual rate-to-hold entry row, then checks
reference retention separately. The 0.00002 rad tolerance is unchanged. The
passing run observes capture error 0.0000000605 rad, zero retention error over
32 ms, and zero capture error after invalid NaN input requests hold. Opposite
sticks, consumer disable, new-session restart and EOF/quit cleanup also pass.
The console and controller implementation did not change for this correction.

## Remaining goal boundary

Confirmed pilot semantics, bounded reference/PID/allocation, estimated-state
feedback, declared disturbances, loss/reset/lifecycle handling, source-only
build/qualification and the operator console have evidence at this finite
synthetic fixture scope. Stock Cessna, H10, original CAD/model and negative-only
C1E-N1 retain their scope; shared uORB and the checkout owner template are unchanged.

SM5 still requires accepted vehicle mass/CG/full inertia, canard geometry/signs
and effectiveness versus airflow, loaded servo calibration/dynamics, EDF
thrust/lag/wash, operating limits and loss/recovery policy. The user reports
measurements unavailable and CAD pending. General publisher-handle identity,
all-topic/output atomicity and production flight-health policy are unqualified.
The finite synthetic results do not establish vehicle trim, an airflow/gain
envelope or static-launch authority. The full goal remains active.
