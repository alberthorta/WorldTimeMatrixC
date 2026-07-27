#!/usr/bin/env python3
"""Genera AppIcon.icns para WorldTime Jitter.

Diseño: el icono es una matriz LED. Fondo squircle azul noche con la retícula de
LEDs apagados visible, y encima un cursor de raton en pixel art "encendido" sobre
esa misma retícula, con unas rayas de movimiento en cian a su izquierda (el
jitter). La idea es que el icono se lea como "lo que hace el WorldTime Matrix",
no como un icono genérico de app.

Todo se dibuja sobre una rejilla de 32x32 celdas para que cada elemento caiga
en un LED exacto — nada de medios píxeles, que es lo que le da el aire pixel art.

Mismo pipeline que ClaudeStats (../ClaudeStats/scripts/make-icon.py): render a
1024x1024, export a todos los tamaños del .iconset y iconutil.

Uso: python3 mac/scripts/make-icon.py
"""

import math
import os
import shutil
import subprocess

from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES_DIR = os.path.join(ROOT, "Resources")
OUT_DIR = os.path.join(RES_DIR, "AppIcon.iconset")
ICNS_PATH = os.path.join(RES_DIR, "AppIcon.icns")

SIZE = 1024
CORNER = 225      # ratio squircle de macOS
# Celdas por lado. 26 (y no 32) para que el cursor ocupe ~60% del icono: con la
# rejilla mas fina el dibujo se queda pequeño en el marco y no se lee a 32 px.
GRID = 26
CELL = SIZE // GRID
# GRID no divide SIZE exacto; centramos la rejilla y repartimos el sobrante.
ORIGIN = (SIZE - GRID * CELL) // 2

# Paleta: la del menu del firmware (Display.cpp) para que app y panel peguen.
CYAN = (56, 189, 248)      # 0x38BDF8, el accent del menu en el panel
WHITE = (248, 250, 255)
INK = (6, 10, 24)          # contorno oscuro del cursor
BG_INNER = (30, 41, 78)
BG_OUTER = (8, 11, 26)

# Cursor clasico en pixel art. X = contorno, O = relleno, . = transparente.
CURSOR = [
    "X...............",
    "XX..............",
    "XOX.............",
    "XOOX............",
    "XOOOX...........",
    "XOOOOX..........",
    "XOOOOOX.........",
    "XOOOOOOX........",
    "XOOOOOOOX.......",
    "XOOOOOOOOX......",
    "XOOOOOXXXXX.....",
    "XOOXOOX.........",
    "XOX.XOOX........",
    "XX..XOOX........",
    "X....XOOX.......",
    "......XX........",
]

# Rayas de movimiento, en coordenadas RELATIVAS al origen del cursor para que
# queden pegadas a el pase lo que pase con el encuadre: (fila, longitud, alpha).
# Todas terminan 2 celdas a la izquierda del cursor y se escalonan hacia abajo,
# asi se leen como estela y no como un bloque suelto.
MOTION_GAP = 2
MOTION = [
    (4,  4, 235),
    (8,  3, 175),
    (12, 2, 120),
]


def squircle(size, radius):
    im = Image.new("L", (size, size), 0)
    ImageDraw.Draw(im).rounded_rectangle([0, 0, size, size], radius=radius, fill=255)
    return im


def radial_gradient(size, inner, outer):
    """Degradado radial suave del centro a los bordes."""
    im = Image.new("RGB", (size, size))
    cx = cy = size / 2
    maxd = math.hypot(cx, cy)
    px = im.load()
    for y in range(size):
        for x in range(size):
            d = min(1.0, math.hypot(x - cx, y - cy) / maxd)
            px[x, y] = tuple(int(inner[i] * (1 - d) + outer[i] * d) for i in range(3))
    return im


def cell_box(col, row, inset=0):
    """Rectangulo de una celda de la rejilla, con margen opcional."""
    x0 = ORIGIN + col * CELL + inset
    y0 = ORIGIN + row * CELL + inset
    return [x0, y0, x0 + CELL - 1 - inset * 2, y0 + CELL - 1 - inset * 2]


def draw_led_grid(base):
    """Retícula de LEDs apagados: puntos redondos muy tenues en cada celda."""
    layer = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    for row in range(GRID):
        for col in range(GRID):
            d.ellipse(cell_box(col, row, inset=CELL // 3), fill=(255, 255, 255, 16))
    return Image.alpha_composite(base, layer)


def draw_pixel(d, col, row, color, alpha=255, round_px=6):
    """Un LED encendido: cuadradito con las esquinas suavizadas."""
    d.rounded_rectangle(cell_box(col, row, inset=2), radius=round_px,
                        fill=color + (alpha,))


def compose():
    bg = radial_gradient(SIZE, BG_INNER, BG_OUTER)
    mask = squircle(SIZE, CORNER)
    base = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    base.paste(bg, (0, 0), mask)
    base = draw_led_grid(base)

    # Origen del cursor en la rejilla. El conjunto a encuadrar no es solo el
    # cursor: son las rayas + el hueco + el cursor. Lo centramos entero, si no
    # la estela se va al borde y queda un vacio raro en medio.
    cursor_w = max(len(l.rstrip(".")) for l in CURSOR)
    trail_w = max(length for _, length, _ in MOTION) + MOTION_GAP
    ox = (GRID - (trail_w + cursor_w)) // 2 + trail_w
    oy = (GRID - len(CURSOR)) // 2

    # Halo cian detras del cursor — da la sensacion de LED encendido de verdad.
    glow = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    for row, line in enumerate(CURSOR):
        for col, ch in enumerate(line):
            if ch != ".":
                gd.rounded_rectangle(cell_box(ox + col, oy + row), radius=8,
                                     fill=CYAN + (70,))
    glow = glow.filter(ImageFilter.GaussianBlur(radius=26))
    base = Image.alpha_composite(base, glow)

    layer = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)

    for row, length, alpha in MOTION:
        end = ox - MOTION_GAP          # ultima celda antes del hueco
        for i in range(length):
            draw_pixel(d, end - i, oy + row, CYAN, alpha)

    for row, line in enumerate(CURSOR):
        for col, ch in enumerate(line):
            if ch == "X":
                draw_pixel(d, ox + col, oy + row, INK)
            elif ch == "O":
                draw_pixel(d, ox + col, oy + row, WHITE)

    base = Image.alpha_composite(base, layer)

    # Recorte final al squircle por si algo se ha salido.
    final = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    final.paste(base, (0, 0), mask)
    return final


def main():
    os.makedirs(RES_DIR, exist_ok=True)
    if os.path.isdir(OUT_DIR):
        shutil.rmtree(OUT_DIR)
    os.makedirs(OUT_DIR)

    master = compose()
    master.save(os.path.join(RES_DIR, "AppIcon-1024.png"))

    for s in [16, 32, 64, 128, 256, 512, 1024]:
        master.resize((s, s), Image.LANCZOS).save(
            os.path.join(OUT_DIR, f"icon_{s}x{s}.png"))
        if s < 1024:
            master.resize((s * 2, s * 2), Image.LANCZOS).save(
                os.path.join(OUT_DIR, f"icon_{s}x{s}@2x.png"))

    subprocess.run(["iconutil", "-c", "icns", OUT_DIR, "-o", ICNS_PATH], check=True)
    print(f"✓ Escrito {ICNS_PATH}")


if __name__ == "__main__":
    main()
