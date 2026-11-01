# Continuous deployment

The Pi runs versioned application releases under /opt/ceilometer/releases and
/opt/ceilometer-firmware is an atomic symlink to the current release. Each release
contains the web service, runtime, tests, source snapshot, Pico UF2 and a SHA-256
manifest. Credentials, measurement data, Git metadata and flash settings are not
part of the artifact. Source and firmware defaults remain 500 MHz / 1.5 V.

## GitHub workflow

.github/workflows/deploy.yml runs on main pushes or manual dispatch. It runs
portable/GUI/Python tests, builds with Pico SDK 2.3.0 and packages the result.
Only successful builds publish a normal fast-forward commit on the deploy branch.
The source revision is recorded in release.json. GitHub Actions dependencies are
pinned to immutable commits. Protect main and restrict who may push deploy.

The Pi updater fetches refs/heads/deploy every five minutes. It requires a
connected, confirmed-disabled instrument; active measurements defer the update.
It checks artifact identity and every file checksum, rejects traversal/symlinks
and unlisted files, runs Python runtime tests on the Pi, and stops the old service.
It then sends a second serial Stop and checks output statuses before flashing.
It addresses exactly the configured Pico serial, only flashes if the UF2 changed,
switches releases, and requires the dashboard to report connected/disabled.
On failure it restores the previous firmware/release and starts disabled.

This is health/transport validation, not independent electrical or atmospheric
qualification. Initial bare-board provisioning is an explicit one-time exception
to the existing-service shutdown check; future automatic updates cannot use it.
The overclock and receiver timing still require hardware qualification.

## Configure the Pi

The deployment installs /etc/ceilometer/cd.env and the update service/timer, but
the timer stays disabled until a real repository URL is configured. Put the
read-only Git URL, explicit deployment ref and target Pico USB serial in cd.env.
Do not embed passwords/tokens in a Git URL. For a private SSH repository, provision
a read-only deploy key and its verified known_hosts entry for root separately.
The updater disables interactive Git authentication and requires known SSH hosts.

```sh
sudoedit /etc/ceilometer/cd.env
sudo systemctl start ceilometer-update.service
sudo systemctl enable --now ceilometer-update.timer
journalctl -u ceilometer-update.service
```

No inbound CI/SSH port or tailnet credential is needed by a hosted runner: the Pi
pulls over its existing internet connection. This is suitable for a Pi behind
NAT/Tailscale. Reconnect/restart/update never automatically enables emission.

The updater can also track a compatible deploy branch from any other Git host;
run package_release.py in that host's CI and publish the artifact contents as
the deployment branch. Do not point it at ordinary source-only main: a tested UF2
and release manifest must be present.

## Manual deployment and rollback

Build and test locally, then create an archive:

```sh
python deploy/releases/package_release.py --revision <source-commit> --output release.tar.gz
```

Copy it to the Pi via SSH/SFTP and invoke the installed installer:

```sh
sudo python3 /opt/ceilometer-firmware/deploy/releases/install_release.py release.tar.gz --pico-serial <expected-serial>
```

To roll back manually, deploy a previously retained archive through the same
installer while disabled; do not manually change firmware while the service owns
USB. Release directories are retained for inspection and recovery.

When replacing the bare test Pico with the real mainboard, update both
CEILOMETER_SERIAL in dashboard.env and CEILOMETER_PICO_SERIAL in cd.env to that
board's actual USB identity. The updater intentionally refuses to guess a target.

The deployed bare-board setup retains 500 MHz and has a persistent operator
lock in dashboard.env: CEILOMETER_INHIBIT_ENABLE. The controller and dashboard
reject every Enable mode while this reason is set. Updates preserve the lock;
only an administrator can remove it after hardware qualification and restart
the service. No update automatically resumes measurement.
