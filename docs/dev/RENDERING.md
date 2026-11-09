# Rendering

The 3D renderer lives in `src/render/` and is built on **SDL_GPU**: Metal on
macOS/iOS, Vulkan on Linux, Android and Windows. PCs whose GPU has no Vulkan
get it through our own implementation of the SDL_GPU calls we use on OpenGL ES
3.0 ([OpenGL ES](#opengl-es)). The 2D UI (`Ui`, `SDL_Renderer`) keeps working
unchanged on top of it.

## Frame structure

```
App::init
  GpuContext::create(window)          SDL_CreateGPUDevice(SPIR-V | MSL)
    SDL_CreateGPURenderer(device, window)   2D renderer on the same device;
                                            it owns the swapchain
  (failure) -> SDL_CreateRenderer(window, nullptr), 2D only

each frame (App::iterate)
  (gamma not 1) GammaPass::begin          the 2D renderer draws into a window-sized target
  Ui::clear
  Screen::render
    View3D::draw(rect, camera, models)    own command buffer:
      render pass -> offscreen RGBA8 (+MSAA resolve, +depth)
      submit
      SDL_RenderTexture(wrapper)          wrapper = SDL_Texture around the GPU texture
    ui.text(...)                          UI on top
  (gamma not 1) GammaPass::end            that target onto the window through gamma.frag
  SDL_RenderPresent                       2D command buffer, submitted after ours
```

The 3D views render into offscreen textures. Each view's command buffer is
submitted before the 2D renderer's, so GPU ordering is guaranteed. The 2D
renderer then composites the view as an ordinary texture
(`SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER`). This means:

- Several 3D views per screen work (e.g. a car preview inside a menu).
- `--shot` and F12 (`SDL_RenderReadPixels`) include the 3D layer without
  extra code.
- HiDPI: the offscreen target is sized in physical pixels (logical
  rectangle × logical-presentation scale), so 3D stays sharp at 640×360 UI
  scale.
- Resizing is handled lazily: the target is recreated when the pixel size
  or MSAA level changes.
- The player's gamma (`[video] gamma`, `render::GammaPass`) is the one pass over the whole
  frame. At 1, the default, it does not run and the frame goes to the window as before.
  Otherwise the 2D renderer draws the frame into an RGBA8 target the window's size (wrapped
  like a 3D view's, `render::wrap_texture`, with the window's logical presentation), and
  before the present draws it over the whole window with `gamma.frag`,
  `out = in ^ (1 / gamma)` per channel: on SDL_GPU as the GPU renderer's own fragment shader
  (`SDL_GPURenderState`, in the frame's command buffer, so in order); on OpenGL ES, whose 2D
  renderer takes no shaders, through our own pass (`gamma.vert`'s triangle) into a second
  target that the 2D renderer then copies (gpu_gles.cpp draws pipelines without vertex
  attributes with no vertex buffer bound, as SDL_GPU does). UI text in the target is rasterised
  at the window's pixel size, as on the window (`render::drawing_to_window`), so the frame
  matches one drawn to the window exactly. Screenshots are read before the pass, so they keep
  GT3's colours (`GT3R_SHOT_GAMMA=1`: after it, as shown). There is none in VR (the canvases go to the headset as they are) or without the 3D
  renderer.
- The dynamic scale allocates nothing: the targets are sized for its ceiling
  (`GpuContext::render_ceiling`) and each frame draws its picture into their
  top-left corner (viewport and scissor), which the 2D renderer upscales with a
  source rectangle half a texel short of the unused part. The post passes map
  each picture they blend (the last frame, the dissolve's frozen one) from the
  scale it was taken at. Recreating the targets at every 5% step would stall
  a Moto G Power 2024 for 90–440 ms each time (SDL allocates a new image per
  target for every frame in flight); with the fixed size, 15 steps in one race
  had a worst frame of 29 ms. `GT3R_DYN_CYCLE=<seconds>` sweeps the scale from
  its ceiling to 50% and back (also in `--shot` runs) for checking.

**SDL version:** this needs SDL ≥ 3.4 (`SDL_CreateGPURenderer`, wrapping GPU
textures). The code compiles against 3.2 (guarded by
`SDL_VERSION_ATLEAST(3, 4, 0)`) and then runs 2D-only. The build, CI and
Android all fetch `release-3.4.12` (`cmake/Dependencies.cmake`); the VR builds
take a pinned commit of SDL's 3.6 development branch ([VR](VR.md#building)).

## Fallback

`GpuContext::create` returns nullptr, and the app logs it and continues
2D-only, when:

- SDL is older than 3.4;
- no GPU backend accepts SPIR-V or MSL (e.g. a PC without Vulkan drivers: we
  don't ship DXIL yet), and OpenGL ES 3.0 is not there either ([OpenGL ES](#opengl-es));
- device, renderer, shader or sampler creation fails;
- the user disabled it: `[video] gpu = 0` in `config.ini` (Settings → Display →
  3D renderer → Off), or `--no-gpu`.

`App::gpu()` is nullptr in that case. Screens must check it.
The model viewer (`ModelViewerScreen`) shows the list and statistics with a
"3D renderer unavailable" note.

A GPU device lost while running (Vulkan's `VK_ERROR_DEVICE_LOST`, which SDL
reports only in its error text) does not come back: `App::iterate` closes the
game with a message.

## Configuration

| `config.ini [video]` | Command line | Default | Effect |
|---|---|---|---|
| `gpu` | `--no-gpu`, `--gles`, `--gles-driver` | auto | Settings → Display → 3D renderer. `auto`: SDL_GPU, then OpenGL ES where built in (Windows, Linux); `gles`: OpenGL ES first; `gles_driver`: OpenGL ES first, on Windows the driver's rather than ANGLE; `0`: 2D only. Takes effect after a restart |
| `msaa` | `--msaa N` | 4 (desktop), 2 (Android: phones, tablets, Quest) | 1/2/4/8, the nearest the device offers: a missing 2x becomes 4x (Mali GPUs offer 1x, 4x and 8x only; on a Galaxy A15 4x cost about 5 points of dynamic resolution), other missing levels the next lower. Phones default to 2x: on a Moto G Power 2024 (Trial Mountain, 6 cars, fixed 60% scale) 4x cost about 19% of the frame and 2x about 3% vs off, and with 4x dynamic resolution sat at its 50% floor. See `platform::default_msaa` |
| `vsync` | | 1 | Present with vsync |
| `aspect` | | 16:9 | Settings → Display → Aspect ratio: the game area of the game's own screens (`Screen::follows_aspect`: its menus, races, movies), 360 UI units tall and 640 (`16:9`), 480 (`4:3`) or the window's shape (`fill`, 480 to 1440 wide) across, letterboxed into the window (`App::layout_ui`, `platform::game_width`). Our own screens keep the 640×360 space or the whole window. 3D views are Hor+ (the vertical FOV is fixed). The race HUD keeps GT3's 640×448 layout in the middle and pushes the elements GT3 pushes in its 16:9 option to the game area's sides: 106.667 frame units on 16:9, more with `fill`, none at 4:3 (GT3's 4:3 layout). Always 16:9 in VR, where Settings has no such row |
| `gamma` | | 1 | Settings → Display → Gamma, 0.50..2.00 in steps of 0.05 (`render::parse_gamma` clamps the file's value): the whole picture through `out = in ^ (1 / gamma)`, higher brighter ("Frame structure"). 1 removes the key, and Settings shows a "Reset gamma to 1.00" row while it is not 1. The pass costs a window-sized target (SDL keeps one per frame in flight) and a full-window draw a frame |
| `limit_60fps` | | off | `on` (Settings → Display → Limit to 60 fps): on 120 Hz and faster displays draw every Nth refresh (120 Hz → 60 fps, 144 → 72) through SDL's main loop rate (`App::apply_frame_limit`; not in VR, where the headset paces the frames); off: every refresh |
| `show_fps` | | off | `on` (Settings → Display → FPS counter): the frames drawn per second, averaged over about half a second, at the top left of the window over everything; on our own screens (`Screen::adaptive_ui`), whose header runs along the top, at the bottom right (`App::draw_fps`). In the safe area on phones, on the virtual screen in VR |
| `full_car_detail` | | on (desktop), off (Android, iOS) | Settings → Display → Always show full car detail: every car at LOD 0 at any distance (`race::car_lod`, `App::full_car_detail`); off: GT3's LODs by distance. On a Galaxy A15 (Trial Mountain, 6 cars) on drew 32% more triangles and 25% more draw calls: +1.4 ms a frame at a fixed 70% scale with `gpusync=1` (+0.7 ms of it CPU), and in normal play about 1.5 points less dynamic resolution at the same 45 fps |
| `showroom_wheels` | | on (desktop), off (Android, iOS) | Settings → Display → Showroom wheels in races: race cars' LOD 0 wheels are the menus' (`menu/cars` rim and 256x128 tyre texture, a fitted wheel's `menu/wheel/<label>`, 30-segment tyres; `CarModelOptions::showroom_car`, `App::showroom_wheels`); a spinning rim is blurred by turned copies of itself (`DrawItem::spin_turn`, [RACE](RACE.md)); off: GT3's race wheels. About 5,200 more triangles a car at LOD 0 (median rim 1,413 triangles against 112), more while the rims spin (a six-car grid at Trial Mountain, 2560x1440: 96K to 400K triangles, about 0.4 ms of GPU and 0.3 ms of CPU a frame on an M4 Pro), and each car reads its `menu/cars` file when the race loads |
| `sharp_menu_models` | | on | Settings → Display → Sharp cars and prizes in menus: the menus' 3D models at the screen's resolution over the 640 x 448 art (`menu::MenuCanvas`, `App::menu_model_size`); off: drawn into the page like the art. Phones and tablets keep models with depth of field (license cards, trophies) in the page: their passes at screen resolution take a whole frame |
| `heat_haze` | | off | `on` (Settings → Display → Heat haze in replays): the replays' heat haze lenses (`race::HeatHaze`, `DrawOptions::haze`; [RACE](RACE.md#passes-after-the-3d-picture)). Off by default: in a sharp picture the lenses, sliding across panning shots and jumping where they wrap, read as jitter rather than GT3's soft shimmer |
| `replay_vignette` | | on | `off` (Settings → Display → Vignette in replays): no replay vignette (`RaceScreen::add_screen_passes`, `DrawOptions::vignette`; [RACE](RACE.md#passes-after-the-3d-picture)) |
| `replay_trail` | | on | `off` (Settings → Display → Motion trail in replays): no trail in replay view mode 1 (`DrawOptions::trail` stays 0; presentation §G16) |
| `depth_of_field` | | off (phones, tablets), high (desktop) | `off`, `standard` or `high` (Settings → Display → Depth of field in replays, `App::depth_of_field`). `standard` builds GT3's own small pictures at any size (`DrawOptions::DepthOfField::pyramid`), `high` builds them at the picture's own resolution above 448 lines. A Moto G Power 2024 on a Super Speedway replay: off 40.4 fps, standard 28.2, high 16.3. At 900 lines the two look alike at normal size (0.5 to 1.5% of pixels more than 8 levels apart); at 1440 lines `standard` shows blocky |
| `gpu_debug` | | 0 | Backend validation layers (OpenGL ES: GL error checks kept) |
| `anisotropy` | | 4 | 1 (off), 2, 4, 8 or 16: the mipmapped samplers' anisotropy (not the `no_mips` ones). A GPU without anisotropic filtering gets none (`GpuContext::anisotropy_supported`) |
| `render_scale` | `--render-scale X` | auto | 0.5..4.0: 3D views render at this fraction of their on-screen pixel size and are scaled (linear) when composited; UI and HUD stay native. `auto` = 1.0 on desktop; on Android/iOS about 900 lines of the 16:9 game area (e.g. 0.63 on a 2560×1600 tablet, 0.70 on a 2856×1280 phone). Above 1 (supersampling) within `platform::supersample_limit`: 16384 pixels a side per view, the largest 2D texture SDL_GPU assumes (400% up to 4096 pixels across, 320% of 5120×2880); no memory cap, and like any 3D resolution it costs frame rate unless dynamic resolution is on. Supersampled targets are not cycled: SDL keeps a cycled target once per frame in flight, and 400% of 3840×2160 with 4x MSAA then took over 20 GB, which SDL's Vulkan backend put in system memory (it only warns, once per device) on a 10 GB RTX 3080: about a frame a second. Uncycled (about 5 GB), the 3080 draws it in 29.9 ms a frame, 10.3 ms with MSAA off; an M4 Pro draws 400% of 2560×1440 at 48–55 fps (42–46 cycled). A picture over twice the on-screen size is halved first by a linear `SDL_BlitGPUTexture` (each pixel a 2×2 mean), so the composite's linear filter misses no texel. A view whose supersampled targets cannot be created steps down to 75% of that size per failed frame, at most to native. See `src/platform/render_scale.*` |
| `dynamic_resolution` | | on (phones, tablets), off (desktop) | Settings → Display → Dynamic resolution: `render_scale` becomes the ceiling (with `auto`, the screen's full resolution up to 1440 lines of the 16:9 game area, `platform::dynamic_ceiling`; it starts at the fixed `auto` size, or with `auto` where the last race's scale ended, `[video] dynamic_start`, saved when a race ends and when the app goes to the background and cleared when 3D resolution or dynamic resolution change; a manual 3D resolution always starts at its own value) and `render::DynamicScale` lowers the 3D scale in 5% steps (to 0.5) when frames miss the target while waiting on the GPU, and steps back up when they keep it. Target: 60 fps, or the first whole number of refreshes at or above 1/60 s with vsync (45 fps on 90 Hz). Judged each second; the 5 s after a load or pause are skipped. Calm on purpose (each change is a visible twitch, each missed refresh a judder): it steps down when frames keep missing a refresh (3 in a second) or are slow on average, skips the second after a step down, and steps up only after 5 s without a miss; a step up that misses is undone and not retried for 1, then 2, then 3 minutes. Off for `--shot`, `--render-scale` and VR. A Galaxy A15 on Tokyo R246 (6 cars): 50–75%, 42–49 fps |

## Shaders

Shaders are authored once in Vulkan GLSL (`src/render/shaders/model.vert`,
`model.frag`; `present.vert` / `.frag` are VR's present pass, [VR](VR.md); `gamma.frag` /
`.vert` the player's gamma, "Frame structure"; `no_color.frag` the depth reset, "GT3 lighting") and
compiled **offline**. The outputs are committed, so CI and
the Android Gradle build need no shader tools:

```
GLSL --glslangValidator--> SPIR-V --spirv-opt -O--> SPIR-V   (Vulkan)
SPIR-V --spirv-cross--msl-> MSL     (Metal, compiled from source at runtime)
SPIR-V --spirv-cross--es--> GLSL ES 3.00 (OpenGL ES 3.0: gpu_gles.*)
```

To regenerate after editing a shader:

```bash
brew install glslang spirv-tools spirv-cross     # or the LunarG Vulkan SDK on Linux/Windows
python3 src/render/shaders/compile_shaders.py
```

This writes `src/render/shaders/generated/model_{vert,clip_vert,frag,frag_plain,frag_filter,frag_resample,frag_fx}.h` (`model_clip_vert`
is `model.vert` with `CLIPPED` defined: the corners of the triangles GT3 clips at the guard
band, see "GT3 lighting"): a 4-byte aligned SPIR-V byte array, the MSL source (entry point `main0`) and the GLSL ES
source with its uniform block and sampler names in slot order (GLSL ES 3.00
has no binding qualifiers, so `gpu_gles.cpp` binds them by name). The GLSL ES
vertex shader flips y and remaps depth to GL's clip space. `model_{vert,frag}_copy.h`
are the same sources built with `SPIN_COPIES` (`DrawItem::spin_turn`, `PipelineKey::spin_copy`);
their GLSL ES fragment shader also has `NO_SAMPLE_MASK` (GLSL ES 3.00 has no `gl_SampleMask`).

`model.frag` builds in several sizes. `model_frag_plain.h` (`PLAIN`, `PipelineKey::plain`) draws
most of a race's pixels: the texel, GT3's per-vertex lighting and fog, and the road sheen, with the
GS's sums. `model_frag_filter.h` (`PLAIN` and `GS_FILTER`, `PipelineKey::gs_filter`) is the same
with the GS's filter, for the wet road's blur and composite (`DrawItem::gs_bilinear`).
`model_frag_resample.h` (`RESAMPLE`, `PipelineKey::resample`) draws the depth of field's pictures
and layers and nothing else. `model_frag.h` adds the cars' reflection passes, the prize scenes'
sphere maps and the post pass, and `model_frag_fx.h` (`FX`, `PipelineKey::fx`) the wheel spin blurs
and the heat haze.
Code in a shader slows every fragment on phone GPUs even where a uniform skips it, and an Adreno 6xx suffers
most: on Tahiti Maze a Nokia G50 (Adreno 619) took 38.0 ms a frame with one shader for everything,
28.0 ms with the rare sampling apart, 19.2 ms with the plain draws apart too. The floors of the
GS's sums cost little on their own (17.7 ms with them, 16.3 ms without). The SPIR-V goes through
`spirv-opt -O`, which those drivers do not make up for (28.0 to 22.3 ms for `model_frag` alone,
19.2 to 17.7 ms with the plain draws apart); the MSL and GLSL ES come from the unoptimised
module.

Binding conventions follow SDL_GPU's rules:

- Vertex uniforms: `set = 1`.
- Fragment textures/samplers: `set = 2`.
- Fragment uniforms: `set = 3`.
- MSL uses `--msl-decoration-binding`, so `[[buffer(0)]]`, `[[texture(0)]]`
  etc. match SDL's Metal slots. Vertex buffers start at 14 and are handled
  by SDL.

**D3D12 (DXIL)** is not generated yet: `dxc` is not readily available on
macOS. On Windows, SDL picks Vulkan because we only offer SPIR-V and MSL. To
add D3D12 later:

1. Run `spirv-cross --hlsl --shader-model 60` on the SPIR-V. Remap the
   bindings to SDL's D3D12 spaces: vertex `b0, space1`; fragment
   `t0/s0, space2` and `b0, space3`.
2. Compile with `dxc -T vs_6_0/ps_6_0` and add `SDL_GPU_SHADERFORMAT_DXIL`
   to the device and `make_shader`.

## OpenGL ES

PCs whose GPU SDL_GPU cannot drive (no Vulkan driver: NVIDIA GeForce 400/500,
Intel HD 2000–4000, AMD Radeon HD 2000–6000 and their laptop versions) get the
3D renderer on OpenGL ES 3.0, the feature level of Direct3D 10 hardware.
`src/render/gpu_gles.*` implements the SDL_GPU calls the renderer makes on it;
builds with `GT3R_GLES` (Windows and Linux by default) include it.

- **Routing.** `gpu_gles.h` maps those SDL_GPU calls onto `gpu_gles.cpp`
  (macros, so the renderer's code is the same on every backend), which hands
  each one on to SDL unless the OpenGL ES device is in use (`gles::active()`).
  `GpuContext::init` tries SDL_GPU, then OpenGL ES; `[video] gpu = gles`
  (`--gles`) turns the order round.
- **Context.** SDL's GLES2 2D renderer owns the OpenGL ES 3.0 context; 3D views
  render into GL textures it composites
  (`SDL_PROP_TEXTURE_CREATE_OPENGLES2_TEXTURE_NUMBER`), as on SDL_GPU. Every
  call that touches GL first flushes the 2D renderer (which also drops its
  cached GL state) and leaves the state it expects. On a window that has no GL
  yet, SDL sets it up and asks for OpenGL ES 2.0, so a 2.0 context gets one
  more try with 3.0. The GL functions come from the context
  (`SDL_GL_GetProcAddress`, with the Khronos headers in `third_party/khronos`).
  Unless `gpu_debug` is on, the context skips GL error checks where it offers
  that (`SDL_GL_CONTEXT_NO_ERROR`): less work per call, mostly in ANGLE.
- **Windows: ANGLE.** With `libEGL.dll` and `libGLESv2.dll` next to `gt3r.exe`,
  SDL takes ANGLE (OpenGL ES on Direct3D 11, on any Direct3D 10.1 GPU) over the
  driver's own OpenGL ES (`SDL_HINT_OPENGL_ES_DRIVER`), and ANGLE draws
  straight into the window's swap chain instead of copying each frame there
  (`EGL_ANGLE_experimental_present_path`). Settings → Display → 3D renderer →
  OpenGL ES (driver) (`[video] gpu = gles_driver`, `--gles-driver`) asks for the
  driver's instead; SDL still uses ANGLE for a driver without OpenGL ES 3.0
  contexts. `SDL_OPENGL_ES_DRIVER=0` or `1` in the environment overrides both. Release packages ship it: CI builds
  ANGLE with vcpkg's `angle` port (`packaging/angle.ps1`, kept in CI's cache), and `-DGT3R_ANGLE_DIR=<folder>` copies a
  build next to the game and into the package. Without ANGLE, NVIDIA and AMD
  drivers offer OpenGL ES 3.0 contexts themselves (NVIDIA from GeForce 400 on);
  older Intel ones do not.
- **Choices.** Settings → Display → 3D renderer lists the kinds of OpenGL ES
  that open on the computer (`src/app/gles_probe.*`): the one in use, and each
  other kind tried in a copy of the game started with `--probe-3d angle|driver`
  when the Display page first opens (it opens the device on a hidden window as
  a start would, and exits 0 when that works). A copy, because one process can
  set up only one kind of OpenGL, and a driver that crashes or hangs there
  takes only the copy down (no answer in 10 s counts as no). The choices appear
  once the copies answer, in about a second; the setting's own choice stays
  listed, marked as not available, when it does not open.
- **Laptops with two GPUs.** `gt3r.exe` exports `NvOptimusEnablement` and
  `AmdPowerXpressRequestHighPerformance`, so OpenGL ES runs on the fast GPU
  rather than the integrated one (SDL_GPU picks it itself).
- **Conventions.** Render targets keep SDL_GPU's layout (top row first) and
  depth range: the GLSL ES vertex shaders flip y and remap z from [0, w] to
  [-w, w], and the backend flips the front face to match. The remap gives up
  the reversed depth's extra precision far away.
- **Passes.** Copy passes and blits (`glBlitFramebuffer`, linear for the
  supersampled picture's halving) run immediately. Render passes are recorded
  and run at `SDL_EndGPURenderPass`: all uniform data pushed for the pass goes
  up in one buffer upload (bound per draw with `glBindBufferRange`), and the
  draws replay with only the state that changes. With base vertex draws
  (OpenGL ES 3.2, `OES_` or `EXT_draw_elements_base_vertex`) the vertex
  attributes stay put; without, their offsets move by the draw's vertex offset.
  Multisampled targets are renderbuffers resolved with `glBlitFramebuffer`
  (into a cube face for the environment map). Fences wait for the whole GPU
  (`glFinish`); only screenshots and readbacks use them.
- **One thread.** GL objects belong to the context, on the main thread, so
  `GpuContext::warm_pipelines` builds nothing ahead on OpenGL ES.

The log names the context: `OpenGL ES: <GL_VERSION>; <GL_RENDERER>`, e.g.
`ANGLE (NVIDIA, ... Direct3D11 vs_5_0 ps_5_0, ...)` for ANGLE, and so does
Settings → Display → Graphics driver.

Measured (2026-10-31, `--shot` runs of a 6-car Trial Mountain race, 1280×720,
4x MSAA, vsync off): on an RTX 3080 with an i7-12700K, ANGLE and NVIDIA's own
OpenGL ES 3.2 take the same 1.9–2.8 ms a frame (1.3–2.1 ms of it the scene
draw on the CPU) and Vulkan 1.3–1.6 ms (0.7–1.0); on an Intel HD 520 (Mesa
25.2) OpenGL ES takes 19–26 ms and Vulkan 25–28. The pictures match Vulkan's:
0.2% of the pixels differ by more than 16 of 255, along the edges of
alpha-tested foliage (texture sampling differs between the APIs), and none on
the HD 520.

## Materials

There is one shader pair. Per-primitive state comes from
`scene::PrimitiveFlags`:

| Flag | Pipeline / shader effect |
|---|---|
| none | opaque, back-face culled (CCW = front), depth write |
| `kAlphaBlend` | src·α + dst·(1−α), drawn after opaque, in model order |
| `kAdditive` | src·α + dst, drawn after opaque |
| `kAlphaTest` | `discard` if α ≤ `Primitive::alpha_ref` when ≥ 0 (the GS AREF from the node program), else `DrawOptions::alpha_ref` (0.5) / `blend_alpha_ref` |
| `kAlphaBlend \| kAlphaTest \| kAlphaFailBlend` | GS AFAIL = RGB_ONLY (course foliage): drawn twice, the fragments passing the test in the opaque phase (with depth write), the failing ones blended in the blended phase without depth write |
| `kDoubleSided` | no culling; normals flipped for back faces |
| `kUnlit` | colour = texture × vertex colour, no lighting |
| `kVertexColor2x` | the vertex RGB are the GS bytes (0x80 = 128): `model.vert` scales them by 255/128 (GTM1 packets with a colour factor past 0x80; texel × colour >> 7 as on the GS) |
| `kEnvironmentMap` | placeholder reflection: sky/ground gradient mixed by `env_strength` |
| `kNoDepthWrite` | depth write off (the depth test stays on, GREATER_OR_EQUAL as for every pass) |
| `kAdditive \| kDestAlphaAdd` | GS ALPHA 0x58 (Cs·Ad + Cd): with `DrawItem::frame_alpha`, added weighted by the frame-buffer alpha (`BlendMode::DestAlphaAdd`); elsewhere plain additive (src·α + dst). The course road sheen and other blended ALPHA 0x58 course packets |
| `Material::specular_local` | the road sheen's VU program (chunk kind 4): the texel × the first light's specular only, Phong with a local viewer 2 m above the camera (× `DrawItem::modelview_scale` rules), not clamped, fogged to black (see "GT3 lighting") |
| `kAdditive \| kEnvironmentMap \| kUnlit` (or untextured) | car reflection pass (PS2 ALPHA Cs·Ad + Cd): `env_color(reflect(view, n))` × min(vertex RGB × weight, 255/128) × `DrawOptions::reflection_strength`, added with weight = vertex alpha × texture alpha. The bound texture is the base pass's texture with its UVs; its alpha is the per-pixel reflectivity (0 in cut-outs). The per-vertex weight is the primitive's reflection rows `env_c0 + env_c1 × min(V·N, 0)` (decoded from the node program), else `1 + reflection_fresnel × min(V·N, 0)`. See docs/formats/car_model.md. |
| `DrawOptions::env_sphere` | car reflection passes sample this 2D sphere map in eye space (GT3's kernel: s = 0.5 + 0.5 Rx / m, t = 0.5 − 0.5 Ry / m, m = \|R + (0, 0, 1)\|) instead of the cube / gradient: the menu showrooms' fixed reflection image |
| `DrawOptions::quantize_rgb5` | every fragment stored at 5 bits per channel (c8 >> 3 << 3): set while the race renders GT3's reflection map (a PSMCT16 frame buffer) into the cube, so the reflection lookup filters between quantised texels as the GS does |
| `kMultiplyDest` | GS ALPHA 0x89 (Cd·As): destination × fragment alpha (prize cup rims) |
| textured `kEnvironmentMap` with `DrawItem::sphere_map` | prize scenes: the model's own sphere map, UVs from the reflected view vector in view space (GT3's reflection kernel), vertex colour × the reflection rows |
| `DrawItem::frame_alpha` (race course scenery) | the PS2's frame-buffer alpha: every pass writes its fragment alpha unblended (`AlphaWrite::Write`; `kKeepAlpha` = FBMSK 0xFF000000 keeps it), `kAlphaOnly` passes (FBMSK 0x00FFFFFF shadow masks) write only the alpha, and the road sheen is added as Cs·Ad (`BlendMode::DestAlphaAdd`, GS ALPHA 0x58). `kAlphaOnly` primitives of other items are not drawn |
| `kDestAlphaTest` (car reflection passes) | GS TEST.DATE with DATM 0: drawn only where the frame alpha < 0x80. The fragment is discarded where its weight (texel × vertex alpha) reaches 0x80 (`output_mode.w`); the 0x80 an earlier reflection pass leaves is the decoder's part (it drops the triangles the pass repeats). See docs/formats/car_model.md |
| `kDepthOnly` | GS FBMSK 0xFFFFFFFF with depth writes on: only the depth is written (`PipelineKey::depth_only`, colour write mask 0), in the blended phase in model order (car glass drawn without depth writes gets its depth after its reflection) |

`View3D::render_env_cube` renders the six faces with the standard cube-map frames
(the sampler's convention: row 0 of a side face is +Y, of +Y / -Y it is -Z / +Z)
in SDL_GPU's top-row-first order. OpenGL's usual face table (up -Y for the side
faces) assumes bottom-up rows; used here it turned every face half a turn, so
surfaces facing down reflected the sky and those facing up the black below the
horizon.

Alpha is otherwise never written to the colour target (write mask RGB): the
3D view stays opaque when composited and in screenshots. When a
`frame_alpha` item wrote it, the view is composited with a colour-only blend
(the 2D frame keeps its own alpha).

Lit materials use one directional light plus ambient (`DrawOptions`), and up
to three more (`fill_*`, or `ItemLights` per item). Vertex colour is used
as-is (RGBA8 → 0..1). Decoders should map PS2's 0x80 = 1.0 convention
themselves (the GTM1 decoder does for colours that modulate a texture or feed the
lighting; a pre-lit untextured colour stays as the GS shows it, 0x80 = mid grey).
A factor can pass 1.0 (up to 0xFF = 255/128: the GS's MODULATE is texel × colour >> 7,
clamped to 255), which RGBA8 at 0x80 = 255 cannot hold: packets with a colour past 0x80
keep the GS bytes and carry `kVertexColor2x` (Trial Mountain's scenery: 31% of its pre-lit
channels are 0x90-0xBF; other US courses about 1-2%).

### GT3 lighting (primitives with a valid `scene::Material`)

The VU formula (docs/formats/car_model.md): Me + La·Ma + Σ Ld·Md·max(N·L, 0) +
Σ Ls·Ms·max(N·H, 0)^p, times the packet scale (1 or 255/128), clamped to
255/128. Textured: MODULATE (texel × lit, 0x80 = 1.0). Untextured: the GS
shows the lit byte directly (255 = 1.0), so the shader multiplies by 128/255.
As on the VU, the formula is evaluated per vertex in `model.vert` (lights and
material in vertex uniform slot 1, `VertexLighting`), with GT3's infinite
viewer (H from the view's z axis, not the direction to each vertex), clamped per
vertex to 0..255/128 and interpolated across the triangle like GS Gouraud shading:
linearly in screen space (the vertex shader writes the values times the clip w and
the fragment shader multiplies them by `gl_FragCoord.w`, which is what `noperspective`
does: a Mali-G52 driver's shader compiler crashed on that qualifier). Highlights are as
soft as the original's on its
coarse meshes, and the long course strips keep the original's shading.

The road sheen (`Material::specular_local`, chunk kind 4) runs GT3's specular-only
program instead: viewer E = the camera + 2 m along its up axis (GT3's light model
viewer (0, 2, 0) in eye space; for `DrawItem::modelview_scale` s: s² × camera + 2 s
× up), D = vertex − E, R = D − 2 (D·N) N, c = max(0, L·R / |D|)^p with the first
light only, byte = trunc(s Ls Ms c) taken modulo 256 (the GS keeps the low 8 bits),
no ambient or diffuse. `render::sheen_vertex_bytes` is the same on the CPU (tests).

Course environment passes (chunk kinds 2 and 3, `Primitive::env_kind`, items with
`DrawItem::sphere_map`) get their UVs and colour weight per vertex too, as the VU makes
them (`VertexLighting::eye_dir.w` 3 / 4 with the camera's position): kind 2 the sphere map
of the reflected view vector in view space, kind 3 GT3's panoramic lookup (the yaw from
`Primitive::env_point` or the camera's forward, `DrawOptions::panorama` = Env0
+0xEC0 / +0xEC4); then texture x vertex colour like any unlit packet. Prize scenes keep the
per-fragment sphere kernel (`params.w = 4`). See docs/formats/course.md "Lighting".

GT3 fog (`DrawOptions::fog_gt3`, course Env0) is computed per vertex too, from the
view depth W like the VU's transform: F = trunc(b' + 0.1 + (255 − b') min(W, A) / W),
b' = 255 b, fog amount 1 − F / 255, interpolated in screen space. A course whose Env0 has
no fog (b = 1) is still fogged, at F = 255 everywhere, as its packets are on the GS.

The GS works in 8-bit integers and truncates where a GPU rounds, so `model.frag` does its
sums as the GS does, in the textured passes (base, sheen, car reflections, prize sphere
maps): the filtered texel truncated to 8 bits (`gs_texel`), MODULATE = texel × colour >> 7
clamped to 255 (`gs_color`; after GT3's lighting or our own), then fog
(F C + (256 − F) FOGCOL) >> 8 (`gs_fog`: F = 255 still takes a colour past the fog colour
down a level, and a pass fogged to black, the sheen, loses one). Each result is a whole
8-bit level, which the RGBA8 target stores exactly. The blend unit still rounds its
product (Cs × As or Cs × Ad), where the GS truncates ((A − B) C >> 7 + D); a weight of 0x80
is exact either way, as on the road under the sheen. `render::gs_pixel` is the same on the
CPU (tests). With float sums the race frame came out 1 to 3 levels above the original's
(the arcade grids: 1.015 of it on average, now 0.998; Super Speedway's road 1.04, now 1.01;
Route 5's lit windows, past 0x80 and fogged after the clamp, now near the original's).

GT3's VU clips a triangle at the guard band before the GS sees it (twice the screen,
|x|, |y| = 2 w, and the projection's near and far planes: docs/formats/course.md "Lighting"), so
the colour the GS interpolates in screen space spans the clipped pieces. Items with
`DrawItem::guard_band` get the same in views with `DrawOptions::guard_near` (the race's
main views, `CameraRig::gt3_near`): the course models and the wet road's mask, which keep
their geometry on the CPU (`GpuModel`'s keep_geometry). Each frame `View3D::upload_clipped`
finds their triangles with a vertex outside (the indices ascending are GT3's strip order),
clips them with `render::guard_clip` and uploads the corners as `ClipVertex` (the triangle's
three vertices and the corner's weights). `model.vert` built with `CLIPPED`
(`PipelineKey::clipped`) lights the three vertices, blends them and rounds a new corner's
colour bytes as the VU does. The primitive's own draw leaves those triangles out (`v_fog_w.y`,
discarded in `model.frag`) and the clipped draw drops any the GPU does not find outside, so
each triangle is drawn once. These items' vertex colour is linear in screen space as well.
`GpuModel::MeshInfo::primitive_spheres` let the CPU skip the primitives no plane can cut.
Not clipped: the mirror, VR, offscreen views and the reflection map.

GT3's far plane (`DrawOptions::far_clip`, Env0 +0x20 through `CameraRig::gt3_far`: the race's
views, the race behind the quick menu and VR) is the race view's only draw distance: a mesh
wholly beyond it is culled, the guard band clips its items there (the `+z` plane), the other
items are cut at it in `model.frag` (`v_fog_w.y` = the depth past it, as the GPU cuts at its own
far plane) and `DrawItem::far_whole` items (the cars, as GT3's cars pass drops a model whose box
reaches past it) are dropped whole. Background items are never cut, and an item's modelview
scale moves its plane: the backdrop's lies 100 times as far, where the projection's far plane is
(`CameraRig::set_gt3_far`). `GT3R_NO_FAR_PLANE=1` draws as before (2500 m, a 6000 m projection).
The backdrop is drawn whole (`in_order`, its moving nodes joined) right after the sky, and then
the depth is reset to the far value (`DrawItem::depth_reset`: a viewport quad through the
`PipelineKey::depth_reset` pipeline, compare always, no colour, `no_color.frag`: through
`model.frag`, it cost phones over a millisecond), as GT3 resets its Z before the
course (presentation §M2): the course covers the backdrop wherever its planes lie. So the
scenery draws, as GT3's does, only the nodes of the view's visibility list
(`DrawItem::nodes`, docs/dev/RACE.md): the parts a hill hides from that stretch of the track
are not drawn over the backdrop. A node changing lists fades with `DrawItem::fade`: `model.frag`
discards the pixels whose 4 x 4 ordered-dither threshold lies at or above the share
(`wheel_blur.z`), so the node keeps its depth, blend and draw order and needs no other pipeline.
`DrawItem::light_scale` (road shade) scales every light, fills included.
Light sources per scene:

| Scene | Lights |
|---|---|
| race (day) | cars: Env0 car lights 0..3 enabled by Env0 +0x10 bits 0..3 (`race::SunParams::day_car_lights`): light 0 = sun, the others fills (often a negative-diffuse light from below); ambient = global ambient + the enabled lights' ambients; world space |
| race course | the course items' `ItemLights`: Env0 scene lights 0..3 enabled by +0x10 bits 11..14 (`race::SunParams::course_lights`), global ambient 0; slots for the enabled lights with a non-zero diffuse or specular, ambient = the enabled lights' ambients |
| race (point-light courses) | per car `ItemLights` (up to 4 nearby lamps as directional lights) |
| showroom | `menu/showroom_<kind>.fbi`: global ambient + 3 lights in eye space |
| prize scenes | the `.prm` lights in scene space (turned by the view), global ambient 0 |

Textures are uploaded as RGBA8 with a full mip chain built on the CPU (2×2
box filter; SDL's Vulkan mipmap blits go zero-sized on non-square textures,
which loses the device on Mali GPUs) and linear/trilinear filtering with
`[video] anisotropy` (default 4×; Settings → Display → Anisotropic filtering
recreates the samplers). Models share textures with identical texels
(`GpuContext::acquire_texture`: a race asks for ~3,700, ~840 distinct, and a
Mali driver spends ~185 KB on each texture object); a CLUT-animated texture
becomes its model's own before its first update. Each `Uploader` batch goes
through one transfer buffer. A `GpuModel`'s per-frame updates
(`update_vertices`, `update_geometry`) go through a transfer buffer the model
keeps (mapped with cycling) and one command buffer per update;
`update_geometry` skips the index buffer when its contents did not change.

Pipelines (one per `PipelineKey`: blend, cull, depth write, alpha write, coverage, lines) are
created on first use and cached in `GpuContext`. SDL 3.4's Vulkan backend has no pipeline
cache, and a pipeline takes ~65 ms to create on a Galaxy A15 (Mali), a stutter the first time a
combination appears mid-race (~200 ms on a Moto G Power 2024, Imagination BXM). Desktop drivers
are as slow without their own shader cache (the first runs after an update or a driver install:
50-190 ms per pipeline on an RTX 3080, a 190 ms stall at a race's GO). So everywhere
`GpuContext::warm_pipelines` creates the ~175 combinations races and menus use on a
background thread at start-up (about 16 s on the A15 and a Nokia G50, ~37 s on the Moto, 0.4 s on an M4 Pro,
while the opening movie and title play): the frame's own combinations, then the same for
transparent pictures (coverage: car models on the race's gap pop-ups and results bar, the
online pickers' cars), whose first mid-race pop-up otherwise stalled the Moto for 0.5 s. Others
are still created on first use, and the log names each one ("3D pipeline built on first use").
On Android the compiling thread is named after the key while the driver works ("pipe" + blend,
cull, depth write, lines, coverage, alpha, "x" MSAA, e.g. `pipe100000x4`), so a crash report
names the pipeline, and the start-up log line "3D renderer: GPU …, driver …" gives the device
name and driver version.

A Mali-G52 driver (r25p1, Android 11: Moto G20, realme C21Y) crashed in its shader compiler
(a null read in `cmpbe_v2_compile_multiple_shaders`) on the first pipeline it built, on any
thread, at any MSAA level. Tried one by one on a Moto G20 (Firebase Test Lab, see
[Platforms](PLATFORMS.md)): the crash needs `noperspective` varyings whose values the vertex
shader computes (the lit colour and fog). The same shaders without the qualifier, constant
`noperspective` outputs and the 0.2.0 shaders all compiled; optimised or inlined SPIR-V, no
`discard`, no grade pass and 1x MSAA still crashed. The model shaders now make those values
screen-linear by hand (above), and the background warm-up builds all 72 pipelines there.

There are eight samplers: the `clamp_u`/`clamp_v` combinations (otherwise repeat), each with
mipmaps or without (`scene::Texture::no_mips`: the full-size image only, no anisotropy; the
tyres).
`texture = -1` binds a 1×1 white texture.

Out-of-range indices are dropped on upload. A primitive's `index_count` is
rounded down to whole triangles.

Wireframe draws a per-mesh line list of unique edges, built on upload. It
uses a slightly depth-biased projection, so it works without
`fillModeNonSolid` (often missing on Android Vulkan drivers).

## Large scenes

`DrawOptions` options used by the race scene (all off by default):

| Option | Effect |
|---|---|
| `frustum_cull` | skip meshes whose bounding sphere (`GpuModel::MeshInfo::center/radius`, computed on upload) is outside the frustum |
| `draw_distance` | skip meshes further than this; `DrawItem::draw_distance` overrides per item (0 = never) |
| `far_clip` | GT3's far plane (eye depth): what lies beyond is not drawn (see "GT3 lighting" above) |
| `sort_blended` | blended primitives drawn back to front by mesh distance instead of model order |
| `fog`, `fog_color/start/end/max` | linear distance fog (not on background or helper items); `fog_gt3`: GT3's per-vertex depth fog (see "GT3 lighting") |
| `blend_alpha_ref` | alpha-test threshold for blended `kAlphaTest` primitives |
| `post_grade`; `snapshot`, `dissolve`, `trail` | full-screen passes over the finished picture, as GT3's before its 2D layer: the colour grade (levels, a curve per channel, saturation, in GT3's 8-bit table arithmetic; `post_grade_pixel` is the same on the CPU) and, in replays, a dissolve from a frozen picture and a trail of the last one |
| `dof` | GT3's replay depth of field, before those passes: the scene pass keeps its depth; five small single-sample pictures (`PipelineKey::single`, model.frag's `RESAMPLE` variant) downsampled from the picture, then a pass over the (multisampled) target draws the layers of `dof_layers` as quads at their depth, tested GEQUAL against the scene's depth. The OpenGL ES backend keeps the depth between the passes (a stored depth is not invalidated) |
| `haze`; `overlay`; `vignette` | `render::ScreenBatch` triangles in their own rectangle's units, in GT3's order after the scene and `dof` ([RACE](RACE.md#passes-after-the-3d-picture)): `haze` samples a copy of the picture (the scene pass, or the depth of field's last pass, then stores its picture and depth, and a second pass draws the haze, the `DrawItem::late` items and the overlay; depth-of-field frames get that pass too), `overlay` is added before the grade (sun glare, lamp glows), `vignette` is blended over the graded picture before the dissolve and the trail. Uploaded once per draw (one vertex and index buffer); the model shader's unlit and post modes, no shader of their own |
| `DrawItem::late` | drawn after every other item of the pass, with the scene's depth (the tyre smoke) |
| `DrawItem::spin_turn` | a modelled rim's spin blur (showroom wheels): the item is drawn as n instances turned about its own x axis by k / n of the turn (the `SPIN_COPIES` shaders). Each copy writes only its share of each pixel's samples (`gl_SampleMask`), so the resolve averages the copies a pixel holds and each copy's own depth keeps the brake parts behind its spokes sharp; past the sample count the copies take cells of a 4 x 4 ordered pixel pattern (`spin_copy_at`), and after the scene `spin_resolve` filters each rim's place on screen with a kernel weighing every cell alike (two or four bilinear taps a pixel apart each way: a 3 x 3 tent or 1 2 2 2 1), at the weight of how far apart a pixel's copies lie, for the mean of all of them. n doubles from the sample count while the copies would lie more than 1.5 pixels apart at the rim's edge, up to 64 and 16 cells and no more than fit 1.5 pixels apart over a radian of the edge (`spin_copy_count`). OpenGL ES 3.0: a copy a pixel. Views without the filter (VR, offscreen, the reflection cube): a copy a sample |

`DrawItem::background` draws an item first, without depth writes, culling or
fog (sky domes; pass `translate(eye)` as the transform). `DrawItem::in_order`
draws an item whole at its place in the list, as the PS2 sends a model: its
opaque meshes, then its blended ones in model order, before the opaque meshes of
later items and outside the `sort_blended` pass (the race's course scenery,
wet-road frame passes and car shadows, so passes without depth writes stay
under the cars drawn after them). `DrawItem::in_order_join` makes an in-order item part
of the in-order item before it (its opaque meshes after that item's opaque ones, its blended
ones after that item's blended ones): the course's moving nodes, a model of their own, keep
their opaque passes under the scenery's translucent ones (fences, light shafts). `View3D::stats()`
returns meshes drawn/culled, draw calls and triangles of the last draw.

`View3D::encode` sets the pipeline, buffers, textures and uniforms only when they differ from
what the pass has, and a primitive whose indices continue the previous one's in the same mesh,
with nothing changed in between, joins its draw: the same triangles in the same order, so the
picture is the same (Metal and Vulkan byte for byte; ANGLE can differ by 1/255 on a few edge
pixels, less than its own run-to-run differences). GT3's packets hold 20-30 triangles each: on
a 6-car Trial Mountain race this cut the draws by about a third (1,668 to 1,106 a frame in the
main view) and the scene's CPU time by about a quarter.
`math3d.h` has `rotate_x`, `rotate_z` and `rotate_quat`.

## API summary

| Header | What |
|---|---|
| `render/gpu_context.h` | `GpuContext` (device, 2D renderer, pipelines, samplers), `Uploader` |
| `render/gpu_model.h` | `GpuModel` (upload of a `scene::Model`; `update_vertices`, `update_textures`, `update_geometry` for per-frame changes: billboards, palette animations, moving course nodes, the cars' mud flaps and steering parts), `model_stats()` |
| `render/view3d.h` | `View3D::draw(renderer, rect, camera, items, options)`, `draw_to` (straight into a texture: VR eyes), `DrawOptions`, `WireMode` |
| `render/camera.h` | `OrbitCamera` (pad, mouse, touch, turntable) |
| `render/math3d.h` | `Vec3`, `Mat4` (column-major), `perspective`, `frustum` (off-centre, VR eyes), `Frustum` (screen placement for any field of view), `look_at`, `inverse` |
| `render/test_models.h` | procedural test scenes, ground grid |
| `app/screen_model_viewer.h` | `make_model_viewer(title, items, loader)` |

Coordinate conventions:

- Right-handed, +Y up.
- Clip depth 0..1, reversed: 1 at the near plane, 0 at the far plane (`perspective`,
  `frustum`). Every pipeline tests GREATER_OR_EQUAL, with or without depth writes, against
  a clear of 0. This is GT3's Z buffer (24-bit, its maximum at the near plane, every pass
  tested GEQUAL), so second passes over the same vertices pass on equal depth. In a float
  depth buffer it also keeps the precision at any distance: decals a few millimetres above
  a car body stay in front in the replay cameras' telephoto shots (100-150 m, a field of
  view down to 1.4 degrees), where the usual 0..1 mapping resolved only about 1 cm and they
  flickered against the body.
- `scene::Mesh::transform` is column-major and applied as
  `item.transform * mesh.transform`.
- `Model::compute_bounds` ignores mesh transforms. Decoders with
  transformed meshes should fill `bounds_*` themselves.

## Model viewer

`make_model_viewer(title, items, load)` shows a list and a 3D view. The
highlighted item loads after 0.25 s (or on Cross). If `load` throws, the
error message is shown.

Controls:

- List: Up/Down select, Cross enlarges the view, Square cycles wireframe
  (solid / solid+wire / wire), L1/R1 zoom, Triangle goes back.
- Enlarged view: stick/d-pad orbit, L1/R1 (or L2/R2, right stick) zoom,
  Cross toggles the turntable, Select cycles LOD (0..n, all), Start cycles
  cull override (material/none/back/front), L3 toggles the grid,
  Triangle/Circle return to the list.
- Mouse: left-drag orbits, right/middle-drag pans, wheel zooms.
- Touch: drag orbits, pinch zooms, tap a list item to load it (tap again to
  enlarge).

For screenshots, the `GT3R_VIEWER` environment variable presets state:

```bash
GT3R_VIEWER="item=1,full=1,wire=1,lod=-1,cull=0,spin=0,yaw=0.5,pitch=0.3,zoom=0.8,grid=1" \
  gt3r --screen viewer --shot out.png
```

The demo screen `viewer` ("3D viewer (test scene)") needs no game data, so
`--screen viewer` also works before a disc is chosen.
