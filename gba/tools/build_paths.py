"""Locations for disposable build diagnostics and previews."""
from pathlib import Path
GBA=Path(__file__).resolve().parents[1]
GENERATED=GBA/'build-generated'
def generated_path(name):
    path=GENERATED/name
    (path.parent if path.suffix else path).mkdir(parents=True,exist_ok=True)
    return path
