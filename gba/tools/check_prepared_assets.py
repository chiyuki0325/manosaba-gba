"""Check the prepared GBA assets without touching original game assets."""
from pathlib import Path
import re
GBA=Path(__file__).resolve().parents[1]
SOURCES=('font_data.c','story_data.cpp','story_layout.h','graphics_data.cpp','stage_data.cpp',
         'trial_data.cpp','trial_ui_data.cpp','trial_cutin_data.cpp','witch_book_art.cpp',
         'book_map_data.cpp','reenact_data.cpp','credits_data.cpp','execution_data.cpp',
         'trial_map_data.cpp','home_art.cpp','music_room_data.cpp','warning_screen_data.cpp',
         'story_ui_data.cpp','choice_ui_data.cpp','nameplate_data.cpp','save_ui_data.cpp','sfx_data.cpp')
missing=[str(Path('src')/name) for name in SOURCES if not (GBA/'src'/name).is_file()]
for assembly in (GBA/'src').glob('*.s'):
    for name in re.findall(r'\.incbin\s+"\.\./(assets/[^\"]+)"',assembly.read_text()):
        if not (GBA/name).is_file():missing.append(name)
if missing:raise SystemExit('Prepared GBA assets missing:\n  '+'\n  '.join(missing)+'\nRun make -C gba prepare-assets with the original sources, or restore the prepared assets from the repository.')
print('Prepared GBA assets ready')
