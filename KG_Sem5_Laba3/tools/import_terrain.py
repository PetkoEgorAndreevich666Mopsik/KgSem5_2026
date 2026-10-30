from pathlib import Path
import json
import shutil
import sys
import numpy as np
from PIL import Image

if len(sys.argv) != 2:
    raise SystemExit('Usage: import_terrain.py <folder with Height.png, Albedo_Out.png, Normals_Out.png>')
source = Path(sys.argv[1])
root = Path(__file__).resolve().parents[1] / 'assets/terrain'
maps = root / 'maps'
maps.mkdir(parents=True, exist_ok=True)
height = Image.open(source / 'Height.png')
if height.mode not in ('I;16', 'I;16L', 'I') or height.size != (1024, 1024):
    raise ValueError('Expected a 1024 x 1024 16-bit heightmap')
data = np.array(height, dtype=np.float32)
low, high = float(data.min()), float(data.max())
if not np.isfinite(data).all() or high <= low:
    raise ValueError('Invalid heightmap range')
resized = np.array(Image.fromarray(data).resize((1025, 1025), Image.Resampling.BILINEAR))
values = np.rint(np.clip((resized - low) / (high - low), 0, 1) * 65535).astype('<u2')
for name in ('Albedo_Out.png', 'Normals_Out.png'):
    with Image.open(source / name) as image:
        if image.size != height.size:
            raise ValueError('Terrain texture dimensions must match the heightmap')
    shutil.copyfile(source / name, maps / name)
shutil.copyfile(source / 'Height.png', root / 'Height.png')
tiles = {}
for z in range(4):
    for x in range(4):
        tile = values[z * 256:z * 256 + 257, x * 256:x * 256 + 257]
        name = root / f'height_x{x}_z{z}'
        name.with_suffix('.r16').write_bytes(tile.tobytes())
        name.with_suffix('.pgm').write_bytes(b'P5\n257 257\n65535\n' + tile.astype('>u2').tobytes())
        tiles[x, z] = tile
for z in range(4):
    for x in range(3):
        np.testing.assert_array_equal(tiles[x, z][:, -1], tiles[x + 1, z][:, 0])
for z in range(3):
    for x in range(4):
        np.testing.assert_array_equal(tiles[x, z][-1], tiles[x, z + 1][0])
(root / 'terrain.cfg').write_text('4 257 512 0 60\n')
(root / 'terrain.json').write_text(json.dumps(dict(source='Height.png', sourceResolution=1024,
    sourceMin=low, sourceMax=high, size=512, tiles=4, tileResolution=257,
    minHeight=0, maxHeight=60, albedo='maps/Albedo_Out.png', normals='maps/Normals_Out.png',
    normalSpace='world Y-up'), indent=2))
print('Imported heightmap and both textures; all 24 shared tile edges verified')
