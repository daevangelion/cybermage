from pathlib import Path

root = Path(__file__).resolve().parents[2]
text = (root / 'docs/architecture_port.md').read_text()
assert '## Enhanced deviations' in text
section = text.split('## Enhanced deviations', 1)[1].split('\n## ', 1)[0]
for name, flag, original in (
    ('GPU renderer', '--renderer gl', '--renderer sw'),
    ('Hor+', '--renderer gl', '4:3'),
    ('True pitch', '--renderer gl', 'horizon'),
    ('Modern controls', '--controls modern', '--controls classic'),
    ('Android default', '--es controls classic', 'desktop'),
    ('Automap', '--controls modern', 'DOS'),
    ('SAF import', 'Import folder', '--data'),
    ('Selectors', '--controls modern', 'glove'),
):
    row = next((line for line in section.splitlines() if line.startswith('|') and name in line), '')
    assert row and flag in row and original in row, name
print('Review2 deviations: each enhanced behavior names its activation and Original equivalent')
