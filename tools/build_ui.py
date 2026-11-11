#!/usr/bin/env python3
"""MIND SERIES · monta ui/index.html a partir de ui/src.html + la skin común (common/).
Uso: python3 tools/build_ui.py   (ejecutar desde la raíz del repo después de tocar ui/src.html o common/)."""
import base64, pathlib
root = pathlib.Path(__file__).resolve().parent.parent
b64 = lambda p: base64.b64encode((root / p).read_bytes()).decode()

# Colores de este plugin en la skin v2 (ficha MIND SERIES en Notion)
SKIN = {'A': '#fdf3c4', 'B': '#f1d98a', 'T': '#3a2c06', 'SH': '#a8862f'}

skin = (root / 'common/mind-skin.css').read_text()
for k, v in SKIN.items():
    skin = skin.replace('@@%s@@' % k, v)
skin = skin.replace('@@FRAME@@', b64('ui/frame.jpg')).replace('@@GRAIN@@', b64('common/grain.png'))

src = (root / 'ui/src.html').read_text()
out = src.replace('/*@@NUNITO@@*/', (root / 'common/nunito.css').read_text()).replace('/*@@SKIN@@*/', skin)
assert '@@' not in out.replace('@@ ', ''), 'quedan marcas sin sustituir'
(root / 'ui/index.html').write_text(out)
print('ui/index.html', len(out) // 1024, 'KB')
