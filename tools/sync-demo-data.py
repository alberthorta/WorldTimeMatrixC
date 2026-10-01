#!/usr/bin/env python3
# Genera los datos que necesitan las previews de modos de la web (Game of
# Life, Llama, Plasma, Moire, Nyan Cat) a partir del firmware: los glifos de
# TomThumb (la fuente del panel), la paleta clasica del fuego y los frames del
# Nyan Cat. Los mete en src/IndexHtml.cpp entre /*DEMO-DATA-BEGIN*/ y
# /*DEMO-DATA-END*/. Ejecutar tras cambiar esos datos en Display.cpp.
import json, pathlib, re, sys

root = pathlib.Path(__file__).resolve().parent.parent
display = (root / 'src' / 'Display.cpp').read_text(encoding='utf-8')
fonts = list(root.glob('.pio/libdeps/*/Adafruit GFX Library/Fonts/TomThumb.h'))
if not fonts:
    sys.exit('No encuentro TomThumb.h: compila una vez (pio run) para bajar las librerias')
font = fonts[0].read_text(encoding='utf-8')

# ── TomThumb: bitmaps empaquetados MSB primero y tabla de glifos ──
bm_src = font[font.index('TomThumbBitmaps[]'):font.index('};', font.index('TomThumbBitmaps[]'))]
# Sin los comentarios (/* 0x30 zero */): sus 0x.. no son bytes del bitmap.
bm_body = re.sub(r'/\*.*?\*/', '', bm_src.split('{', 1)[1], flags=re.S)
bitmaps = [int(h, 16) for h in re.findall(r'0x([0-9A-Fa-f]{2})', bm_body)]
gl_src = font[font.index('TomThumbGlyphs[]'):]
glyphs = re.findall(r'\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(-?\d+),\s*(-?\d+)\s*\},\s*/\*\s*0x([0-9A-Fa-f]{2})', gl_src)
chars = '0123456789:/-'
out_font = {}
for off, w, h, adv, xo, yo, code in glyphs:
    ch = chr(int(code, 16))
    if ch not in chars:
        continue
    off, w, h = int(off), int(w), int(h)
    bits = ''
    for i in range(w * h):
        byte = bitmaps[off + i // 8]
        bits += '1' if byte & (0x80 >> (i % 8)) else '0'
    out_font[ch] = [w, h, int(adv), int(xo), int(yo), bits]
missing = set(chars) - set(out_font)
if missing:
    sys.exit(f'Faltan glifos: {missing}')

# ── Paleta clasica del fuego ──
p = display[display.index('static const uint8_t pal[FIRE_PAL][3] = {'):]
p = p[:p.index('};')]
fire = ['#%02x%02x%02x' % tuple(int(v, 16) for v in t) for t in re.findall(r'\{0x(..),0x(..),0x(..)\}', p)]

# ── Frames del Nyan Cat ──
n = display[display.index('static const uint8_t NYAN_FRAMES'):]
n = n[:n.index('\n};')]
rows = [''.join(re.findall(r'\d', r)) for r in re.findall(r'\{([0-9,\s]+)\}', n)]
rows = [r for r in rows if r]
if len(rows) % 21 or any(len(r) != 34 for r in rows):
    sys.exit(f'Frames del Nyan inesperados: {len(rows)} filas')
nyan = [rows[i:i + 21] for i in range(0, len(rows), 21)]

block = ('const DEMO_FONT = ' + json.dumps(out_font, separators=(',', ':')) + ';\n'
         'const DEMO_FIRE_PAL = ' + json.dumps(fire, separators=(',', ':')) + ';\n'
         'const DEMO_NYAN = ' + json.dumps(nyan, separators=(',', ':')) + ';')

dst_path = root / 'src' / 'IndexHtml.cpp'
dst = dst_path.read_text(encoding='utf-8')
begin, end = '/*DEMO-DATA-BEGIN*/', '/*DEMO-DATA-END*/'
a, b = dst.find(begin), dst.find(end)
if a < 0 or b < 0:
    sys.exit('No encuentro los marcadores DEMO-DATA en src/IndexHtml.cpp')
new = dst[:a + len(begin)] + '\n' + block + '\n' + dst[b:]
if new != dst:
    dst_path.write_text(new, encoding='utf-8')
    print(f'IndexHtml.cpp actualizado ({len(fire)} colores de fuego, {len(nyan)} frames de Nyan)')
else:
    print('Sin cambios')
