"""Check imported design project references without invoking or modifying CAD tools."""
import json
import re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
board=ROOT/'hardware/electronics/mainboard'
for project in board.glob('*.kicad_pro'):
    json.loads(project.read_text(encoding='utf-8'))
    assert project.with_suffix('.kicad_pcb').is_file(),str(project)
for table in (board/'fp-lib-table',board/'sym-lib-table'):
    for relative in re.findall(r'\$\{KIPRJMOD\}/([^"\n]+)',table.read_text(encoding='utf-8')):
        assert (board/relative).exists(),str(table)+': '+relative
for sch in board.glob('*.kicad_sch'):
    for sheet in re.findall(r'\(property "Sheetfile" "([^"\n]+)"',sch.read_text(encoding='utf-8')):
        assert (sch.parent/sheet).is_file(),str(sch)+': '+sheet
print('PASS: KiCad project JSON, board counterparts, project-local libraries and child-sheet references')
