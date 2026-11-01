# Electronics sources

`mainboard` preserves the complete KiCad project, A/B board variants, custom
symbols/footprints, 3D models and project-local library tables. No board variant
was silently selected or deleted. `simulations` contains LTspice source studies.
`reference` contains historical firmware-review netlist/hash exports; the KiCad
sources are authoritative and those exports may describe earlier revisions.

`history/kicad-local-history.bundle` preserves all Git refs from KiCad local
history. Inspect separately with `git clone <bundle> <temporary-directory>`;
do not unpack it as a nested live repository. Automatic backups, lock files,
local editor preferences and cached Python files are excluded from the import.
