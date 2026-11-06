"""Writes native/third_party/smaa/SMAA.hlsl.inc: SMAA.hlsl (iryoku's shader, MIT) as a C++ raw string, which
src/platform/native/graphics/smaa.cpp compiles as GLSL 3.30. The file is Latin-1 with two middle dots (0xB7) in a comment's
diagram: they're written as '.' (a GLSL compiler may refuse any byte past ASCII). Run it after replacing SMAA.hlsl."""
from pathlib import Path

folder = Path(__file__).resolve().parents[1] / "third_party" / "smaa"
source = (folder / "SMAA.hlsl").read_bytes().decode("latin-1")
source = "".join(c if ord(c) < 128 else "." for c in source)
assert ')SMAA"' not in source
text = ("// SMAA.hlsl (this folder's, iryoku's, MIT: LICENSE.txt) as a C++ string for src/platform/native/graphics/smaa.cpp, its bytes\n"
        "// past ASCII (two in a comment) as '.'. Made by native/tools/embed_smaa.py: run it again if SMAA.hlsl changes\n"
        'static const char SmaaHlsl[] = R"SMAA(' + source + ')SMAA";\n')
with open(folder / "SMAA.hlsl.inc", "w", encoding="ascii", newline="\n") as out:
    out.write(text)
print(f"SMAA.hlsl.inc: {len(text)} characters")
