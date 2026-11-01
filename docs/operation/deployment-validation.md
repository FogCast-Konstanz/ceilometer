# Deployment validation — 2026-11-01

## Results

The Raspberry Pi at the configured instrument host can read the public
FogCast-Konstanz/ceilometer GitHub repository over outbound HTTPS. Its update
timer is enabled, uses refs/heads/deploy and explicitly targets the test Pico
D5E72DD9B609A5A9. No inbound connection from GitHub is used.

A locally prepared Git deployment repository on the Pi was used to exercise the
same updater while the GitHub deploy branch was unavailable. The updater fetched
and archived its deploy ref, verified the manifest, ran Linux preflight tests,
flashed and verified the Pico, switched to the monorepo runtime, and confirmed
the restarted dashboard and outputs off. A second release with the poller update
was installed without reflashing the unchanged UF2. A repeat poll reported
ALREADY_CURRENT. Final installed test release: c93beb5b914211b7365898b1; firmware
0.6.4. These are locally built integration-test releases, not GitHub CI builds.

All 14 local host test suites pass. The Pi passed release, stream-client,
dashboard and updater tests. Its real systemd updater service completed with
WAITING (exit 0) because GitHub has no deploy branch yet. The timer remains
active/enabled for five-minute polling. Missing branches wait cleanly; network
or authentication failures remain errors and are retried by later timer runs.

## GitHub publication blocker

GitHub Actions run 41305622756 passed host tests but failed compiling main.c
with missing PRId64/PRIu64 formatting macros; the publish step was skipped.
An isolated local branch fix/pi-poll-deployment pins the build to the official,
checksum-verified Arm GNU 15.2.rel1 toolchain used for local firmware builds.
This workflow change has not yet been verified by a successful GitHub run.

Pushing the test branch and dispatching its workflow both returned HTTP 403 for
the available GitHub account. The main branch was not changed remotely. Someone
with repository write/workflow permission must publish/review the branch and
run the deployment workflow. Full GitHub-build-to-Pi deployment therefore remains
unverified, despite the successful Pi-side Git/installer integration tests.

Do not point the updater at the source main branch: it requires a packaged deploy
branch containing release.json. When replacing the test Pico, update the explicit
CD target serial only after identifying the intended production board.


## Completed GitHub-to-Pi validation

Using the user's existing MarianKellerHTWG browser session, workflow run
41308456637 was dispatched on fix/pi-poll-deployment and completed successfully:
https://github.com/FogCast-Konstanz/ceilometer/actions/runs/41308456637

The compiler pin passed the actual Linux firmware build. GitHub published its
deploy branch. The Pi's configured systemd updater then fetched that GitHub
branch over HTTPS and installed release 7232ce7401731536607cfc98 from source
3b1cf49cd3d5499d8a67ded6bc51d9f3ae9b262f. Release hashes and final health were
verified: firmware 0.6.4, connected, DISABLED, outputs acknowledged off and no
fault. Thus the earlier publication blocker is resolved for this manual test.
The workflow fix must still be merged into main to cover future automatic
main-branch deployments; no main-branch merge was performed during testing.
No new access token was created or stored, and no inbound GitHub-to-Pi connection
was needed. The periodic update timer remains enabled.
