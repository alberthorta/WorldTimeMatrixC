"""Inyecta FW_VERSION (string) en cada build a partir de `git describe`.

Convencion: usar tags semver-ish (`v0.1.0`, `v0.2.0`, ...) para releases. En
builds limpios sobre un tag, FW_VERSION = tag. En builds intermedios,
FW_VERSION = "<tag>-<n>-g<sha>[-dirty]". Sin tags todavia → solo el sha.

AutoUpdate compara FW_VERSION (local) contra `tag_name` de la ultima release
en GitHub; si difieren, descarga y flashea. Eso significa que un build local
fuera de un tag (p.ej. en desarrollo) NO matcheara nunca con el tag remoto y
el device intentaria "downgrade" a la release publica. Para evitarlo en
devices de desarrollo, manten el commit local en el mismo tag que la release
o desactiva el auto-update (futuro flag).
"""

import subprocess
Import("env")

try:
    version = (
        subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            cwd=env["PROJECT_DIR"],
            stderr=subprocess.DEVNULL,
        )
        .strip()
        .decode("utf-8")
    )
except Exception:
    version = "unknown"

print(f"[version.py] FW_VERSION = {version}")
env.Append(CPPDEFINES=[("FW_VERSION", env.StringifyMacro(version))])
