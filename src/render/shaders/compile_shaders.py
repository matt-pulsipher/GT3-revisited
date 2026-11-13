#!/usr/bin/env python3
"""Regenerates src/render/shaders/generated/*.h from the GLSL sources.

Shaders are authored once in Vulkan GLSL (model.vert, model.frag) and
compiled offline (model.vert twice: model_clip_vert with CLIPPED defined):

    GLSL --glslangValidator--> SPIR-V --spirv-opt -O--> SPIR-V (Vulkan: Linux, Android, Windows)
    SPIR-V --spirv-cross-----> MSL      (Metal: macOS, iOS)
    SPIR-V --spirv-cross-----> GLSL ES 3.00 (OpenGL ES 3.0: render/gpu_gles.*)

The outputs are committed, so normal builds (CI, Android Gradle) need no
shader tools. Only run this after editing a shader:

    brew install glslang spirv-tools spirv-cross        # or the Vulkan SDK
    python3 src/render/shaders/compile_shaders.py

See docs/dev/RENDERING.md.
"""
import json
import os
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
OUT = HERE / "generated"
# (name, source, preprocessor definitions, extra ones for the GLSL ES build): model_clip_vert is
# model.vert for the corners of the triangles GT3 clips at the guard band (render/guard_band.h);
# model_frag_plain is model.frag for most of a race's pixels (PipelineKey::plain), model_frag_filter
# the same with the GS's filter (PipelineKey::gs_filter), model_frag_resample the depth of field's
# pictures (PipelineKey::resample), model_frag_fx with its rare sampling (PipelineKey::fx); the
# _copy shaders draw the modelled rims' turned copies (DrawItem::spin_turn; OpenGL ES 3.0 has no
# sample masks).
SHADERS = [("model_vert", "model.vert", [], []), ("model_clip_vert", "model.vert", ["CLIPPED"], []),
           ("model_frag", "model.frag", [], []), ("model_frag_plain", "model.frag", ["PLAIN"], []),
           ("model_frag_filter", "model.frag", ["PLAIN", "GS_FILTER"], []),
           ("model_frag_resample", "model.frag", ["RESAMPLE"], []),
           ("model_frag_fx", "model.frag", ["FX"], []),
           ("model_vert_copy", "model.vert", ["SPIN_COPIES"], []),
           ("model_frag_copy", "model.frag", ["SPIN_COPIES", "FX"], ["NO_SAMPLE_MASK"]),
           ("no_color_frag", "no_color.frag", [], []),
           ("present_vert", "present.vert", [], []), ("present_frag", "present.frag", [], []),
           ("gamma_vert", "gamma.vert", [], []), ("gamma_frag", "gamma.frag", [], [])]


def run(*args):
    subprocess.run(args, check=True)


def c_bytes(data: bytes) -> str:
    lines = []
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 16]) + ",")
    return "\n".join(lines)


def c_string(text: str) -> str:
    # Raw string literals have a length limit on MSVC (~16 KB per piece); split by line.
    out = []
    for line in text.splitlines():
        esc = line.replace("\\", "\\\\").replace('"', '\\"')
        out.append(f'    "{esc}\\n"')
    return "\n".join(out)


def gles(spv: str, vertex: bool):
    """GLSL ES 3.00 for OpenGL ES 3.0, plus the uniform block and sampler names in slot order.

    Vertex shaders flip y and remap z from [0, w] to [-w, w], so render targets keep SDL_GPU's
    layout (top row first) and depth range; gpu_gles.cpp flips the front face to match.
    """
    args = ["spirv-cross", spv, "--es", "--version", "300"]
    if vertex:
        args += ["--flip-vert-y", "--fixup-clipspace"]
    src = subprocess.run(args, check=True, capture_output=True, text=True).stdout
    src = src.replace("precision mediump float;", "precision highp float;")
    refl = json.loads(subprocess.run(["spirv-cross", spv, "--reflect"], check=True, capture_output=True,
                                     text=True).stdout)
    blocks = sorted(refl.get("ubos", []), key=lambda u: u["binding"])
    samplers = sorted(refl.get("textures", []), key=lambda t: t["binding"])
    block_names = ",".join(refl["types"][u["type"]]["name"] for u in blocks)
    sampler_names = ",".join(t["name"] for t in samplers)
    return src, block_names, sampler_names


def main():
    OUT.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        for name, src, defines, es_defines in SHADERS:
            spv = os.path.join(tmp, name + ".spv")
            flags = [f"-D{d}" for d in defines]
            run("glslangValidator", "-V", "--quiet", *flags, str(HERE / src), "-o", spv)
            msl = subprocess.run(["spirv-cross", spv, "--msl", "--msl-version", "20000", "--msl-decoration-binding"],
                                 check=True, capture_output=True, text=True).stdout
            # (Phone drivers do not make up for an unoptimised module: 28 -> 22 ms a frame on an
            # Adreno 619. The MSL and GLSL ES come from the plain module, as readable as before.)
            opt = spv + ".opt"
            run("spirv-opt", "-O", spv, "-o", opt)
            spirv = pathlib.Path(opt).read_bytes()
            es_spv = spv
            if es_defines:
                es_spv = os.path.join(tmp, name + "_es.spv")
                run("glslangValidator", "-V", "--quiet", *flags, *[f"-D{d}" for d in es_defines], str(HERE / src),
                    "-o", es_spv)
            # (By the source's stage: names like model_vert_copy do not end in _vert.)
            es, es_blocks, es_samplers = gles(es_spv, src.endswith(".vert"))
            built = src + (" with " + ", ".join(defines) if defines else "")
            header = f"""// Generated by src/render/shaders/compile_shaders.py from {built}. Do not edit.
#pragma once

#include <cstddef>

namespace gt3r::render::shaders {{

alignas(4) inline constexpr unsigned char {name}_spv[] = {{
{c_bytes(spirv)}
}};

// MSL entry point is "main0" (spirv-cross renames main).
inline constexpr char {name}_msl[] =
{c_string(msl)};

// GLSL ES 3.00 (OpenGL ES 3.0: render/gpu_gles.*), with its uniform block and sampler names in slot order.
inline constexpr char {name}_gles[] =
{c_string(es)};
inline constexpr char {name}_gles_blocks[] = "{es_blocks}";
inline constexpr char {name}_gles_samplers[] = "{es_samplers}";

}} // namespace gt3r::render::shaders
"""
            (OUT / f"{name}.h").write_text(header)
            print(f"{name}: {len(spirv)} bytes SPIR-V, {len(msl)} bytes MSL, {len(es)} bytes GLSL ES")


if __name__ == "__main__":
    sys.exit(main())
