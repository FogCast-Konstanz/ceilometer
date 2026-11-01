# Mechanical sources and manufacturing inputs

`cad/exchange`: STEP/DXF; `cad/meshes`: STL; `drawings`: approved-source drawing
exports; `printing`: 3MF projects and machine-specific sliced projects;
`analysis`: telescope studies, videos and mockups. Native parametric CAD sources
were not present in the supplied folder; add them or their cloud-design links
when available. STEP and mesh exports do not recover parametric design history.

`docs/migration/mechanical-import.json` records each imported source and SHA256.
Only byte-identical files with the same destination were deduplicated. Differing
files with identical names are preserved using a source-folder suffix. Printer-
specific `.gcode.3mf` files are retained as reference, not assumed universal jobs.
