from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
source = '\n'.join(p.read_text() for p in (root / 'src').rglob('*.[ch]'))
headers = '\n'.join(p.read_text() for p in (root / 'include').glob('*.h'))
for name in ('render_visibility', 'render_view', 'render_prop_texture', 'r_project',
             'r_new_point', 'ui_load_game', 'ui_draw_player_hand', 'ai_pc_zone_radius',
             'R_RAY_MAX', 'R_MAX_COORD', 'Uniform4f', 'ColorMask'):
    assert not re.search(r'\b' + name + r'\b', source + headers), name
for name in ('vis_project', 'vis_new_point', 'vis_prop_texture'):
    definitions = []
    for path in (root / 'src').rglob('*.c'):
        if re.search(r'\bint\s+' + name + r'\s*\([^;]+?\)\s*\{', path.read_text(), re.S):
            definitions.append(path)
    assert len(definitions) == 1 and definitions[0].parent.name == 'vis', (name, definitions)
actions = (root / 'src/controls/actions.c').read_text()
assert actions.count('i = CM_CONTROL_SLOT1; i <= CM_CONTROL_SLOT0; ++i') == 1
launch = (root / 'src/ai/launch.c').read_text()
assert not re.search(r'static int32_t (add|scaled|cosine|sine)\(', launch)
assert 'extern int ai_reset_aim' not in (root / 'src/ai/pc_launch.c').read_text()
print('Review2 cutovers: obsolete APIs absent; one shared projection/frame-selection implementation')
