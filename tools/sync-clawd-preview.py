#!/usr/bin/env python3
# Copia la logica de Clawd del simulador (tools/clawd-sim.html) a la web de
# administracion (src/IndexHtml.cpp), que la usa para las previews animadas
# de cada animacion. Ejecutar tras cambiar animaciones en el simulador.
import pathlib, re, sys

root = pathlib.Path(__file__).resolve().parent.parent
sim = (root / 'tools' / 'clawd-sim.html').read_text(encoding='utf-8')
dst_path = root / 'src' / 'IndexHtml.cpp'
dst = dst_path.read_text(encoding='utf-8')

m = re.search(r'\n[^\n]*CLAWD-LOGIC-BEGIN[^\n]*\n(?:[^\n]*//[^\n]*\n)?(.*?)\n[^\n]*CLAWD-LOGIC-END', sim, re.S)
if not m:
    sys.exit('No encuentro CLAWD-LOGIC-BEGIN/END en tools/clawd-sim.html')
block = m.group(1)
if ')WTHTML' in block:
    sys.exit('El bloque contiene el delimitador del raw string de C++')

begin, end = '/*CLAWD-LOGIC-BEGIN*/', '/*CLAWD-LOGIC-END*/'
a, b = dst.find(begin), dst.find(end)
if a < 0 or b < 0:
    sys.exit('No encuentro los marcadores en src/IndexHtml.cpp')
new = dst[:a + len(begin)] + '\n' + block + '\n' + dst[b:]
if new != dst:
    dst_path.write_text(new, encoding='utf-8')
    print('IndexHtml.cpp actualizado')
else:
    print('Sin cambios')
