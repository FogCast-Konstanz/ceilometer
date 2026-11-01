# Monorepo migration, 2026-11-01

Destination: `C:/Users/MarianKeller/Repos/hwtg/ceilometer`.
The original `ceilometer-firmware` checkout and Teamprojekt OneDrive folder are
unchanged. All original Git commits are retained. The new tag
`pre-monorepo-0.6.4` preserves a committed snapshot of all firmware/Pi changes
before path moves. No remote repository was published or configured for CD.

`path-map.json` records the old firmware paths and new destinations.
`mechanical-import.json` records source paths, SHA256 and duplicate decisions.
KiCad local Git history is retained as a bundle, not a nested repository.
Electronics project structure and project-relative libraries remain together.

Excluded: unrelated `alt` projects, invoices, third-party literature, personal
KiCad installation/preferences, automatic backups, lock/cache files, credentials
and build outputs. The project purchase list is retained under hardware/procurement.
The running Pi and its service configuration are untouched by this migration.

The instrument is now an importable `fogcast` Python package with console entry
points; the direct-open bring-up GUI remains independent. Release packaging
ships only the instrument runtime and UF2. Generated archive compatibility shims
support the old Pi installer's preflight tests, serial Stop and unit-copy paths
for transition to the new package layout; they are not duplicate source trees.
The new installer can also restore service units from an older release.

Large CAD/office/video sources use local Git LFS tracking. Push both Git refs and
LFS objects to a configured LFS-capable remote when publishing later.
