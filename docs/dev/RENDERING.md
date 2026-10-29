# Rendering

The 3D renderer lives in `src/render/` and is built on **SDL_GPU**: Metal on
macOS/iOS, Vulkan on Linux, Android and Windows. The 2D UI (`Ui`,
`SDL_Renderer`) keeps working unchanged on top of it.

## Frame structure

```
App::init
  GpuContext::create(window)          SDL_CreateGPUDevice(SPIR-V | MSL)
    SDL_CreateGPURenderer(device, window)   2D renderer on the same device;
                                            it owns the swapchain
  (failure) -> SDL_CreateRenderer(window, nullptr), 2D only

each frame (App::iterate)
  Ui::clear
  Screen::render
    View3D::draw(rect, camera, models)    own command buffer:
      render pass -> offscreen RGBA8 (+MSAA resolve, +depth)
      submit
      SDL_RenderTexture(wrapper)          wrapper = SDL_Texture around the GPU texture
    ui.text(...)                          UI on top
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
- no GPU backend accepts SPIR-V or MSL (e.g. a Windows PC without Vulkan
  drivers: we don't ship DXIL yet);
- device, renderer, shader or sampler creation fails;
- the user disabled it: `[video] gpu=0` in `config.ini`, or `--no-gpu`.

`App::gpu()` is nullptr in that case. Screens must check it.
The model viewer (`ModelViewerScreen`) shows the list and statistics with a
"3D renderer unavailable" note.

A GPU device lost while running (Vulkan's `VK_ERROR_DEVICE_LOST`, which SDL
reports only in its error text) does not come back: `App::iterate` closes the
game with a message.

## Configuration

| `config.ini [video]` | Command line | Default | Effect |
|---|---|---|---|
| `gpu` | `--no-gpu` | 1 | Use the SDL_GPU path |
| `msaa` | `--msaa N` | 4 (desktop), 2 (Android: phones, tablets, Quest) | 1/2/4/8, the nearest the device offers: a missing 2x becomes 4x (Mali GPUs offer 1x, 4x and 8x only; on a Galaxy A15 4x cost about 5 points of dynamic resolution), other missing levels the next lower. Phones default to 2x: on a Moto G Power 2024 (Trial Mountain, 6 cars, fixed 60% scale) 4x cost about 19% of the frame and 2x about 3% vs off, and with 4x dynamic resolution sat at its 50% floor. See `platform::default_msaa` |
| `vsync` | | 1 | Present with vsync |
| `limit_60fps` | | off | `on` (Settings → Display → Limit to 60 fps): on 120 Hz and faster displays draw every Nth refresh (120 Hz → 60 fps, 144 → 72) through SDL's main loop rate (`App::apply_frame_limit`); off: every refresh |
| `full_car_detail` | | on (desktop), off (Android, iOS) | Settings → Display → Always show full car detail: every car at LOD 0 at any distance (`race::car_lod`, `App::full_car_detail`); off: GT3's LODs by distance. On a Galaxy A15 (Trial Mountain, 6 cars) on drew 32% more triangles and 25% more draw calls: +1.4 ms a frame at a fixed 70% scale with `gpusync=1` (+0.7 ms of it CPU), and in normal play about 1.5 points less dynamic resolution at the same 45 fps |
| `gpu_debug` | | 0 | Backend validation layers |
| `anisotropy` | | 4 | 1 (off), 2, 4, 8 or 16: the mipmapped samplers' anisotropy (not the `no_mips` ones). A GPU without anisotropic filtering gets none (`GpuContext::anisotropy_supported`) |
| `render_scale` | `--render-scale X` | auto | 0.5..4.0: 3D views render at this fraction of their on-screen pixel size and are scaled (linear) when composited; UI and HUD stay native. `auto` = 1.0 on desktop; on Android/iOS about 900 lines of the 16:9 game area (e.g. 0.63 on a 2560×1600 tablet, 0.70 on a 2856×1280 phone). Above 1 (supersampling) within `platform::supersample_limit`: 16384 pixels a side per view, the largest 2D texture SDL_GPU assumes (400% up to 4096 pixels across, 320% of 5120×2880); no memory cap, and like any 3D resolution it costs frame rate unless dynamic resolution is on. Supersampled targets are not cycled: SDL keeps a cycled target once per frame in flight, and 400% of 3840×2160 with 4x MSAA then took over 20 GB, which SDL's Vulkan backend put in system memory (it only warns, once per device) on a 10 GB RTX 3080: about a frame a second. Uncycled (about 5 GB), the 3080 draws it in 29.9 ms a frame, 10.3 ms with MSAA off; an M4 Pro draws 400% of 2560×1440 at 48–55 fps (42–46 cycled). A picture over twice the on-screen size is halved first by a linear `SDL_BlitGPUTexture` (each pixel a 2×2 mean), so the composite's linear filter misses no texel. A view whose supersampled targets cannot be created steps down to 75% of that size per failed frame, at most to native. See `src/platform/render_scale.*` |
| `dynamic_resolution` | | on (phones, tablets), off (desktop) | Settings → Display → Dynamic resolution: `render_scale` becomes the ceiling (with `auto`, the screen's full resolution up to 1440 lines of the 16:9 game area, `platform::dynamic_ceiling`; it starts at the fixed `auto` size, or with `auto` where the last race's scale ended, `[video] dynamic_start`, saved when a race ends and when the app goes to the background and cleared when 3D resolution or dynamic resolution change; a manual 3D resolution always starts at its own value) and `render::DynamicScale` lowers the 3D scale in 5% steps (to 0.5) when frames miss the target while waiting on the GPU, and steps back up when they keep it. Target: 60 fps, or the first whole number of refreshes at or above 1/60 s with vsync (45 fps on 90 Hz). Judged each second; the 5 s after a load or pause are skipped. Calm on purpose (each change is a visible twitch, each missed refresh a judder): it steps down when frames keep missing a refresh (3 in a second) or are slow on average, skips the second after a step down, and steps up only after 5 s without a miss; a step up that misses is undone and not retried for 1, then 2, then 3 minutes. Off for `--shot`, `--render-scale` and VR. A Galaxy A15 on Tokyo R246 (6 cars): 50–75%, 42–49 fps |

## Shaders

Shaders are authored once in Vulkan GLSL (`src/render/shaders/model.vert`,
`model.frag`; `present.vert` / `.frag` are VR's present pass, [VR](VR.md)) and
compiled **offline**. The outputs are committed, so CI and
the Android Gradle build need no shader tools:

```
GLSL --glslangValidator--> SPIR-V   (Vulkan)
SPIR-V --spirv-cross--msl-> MSL     (Metal, compiled from source at runtime)
```

To regenerate after editing a shader:

```bash
brew install glslang spirv-cross     # or the LunarG Vulkan SDK on Linux/Windows
python3 src/render/shaders/compile_shaders.py
```

This writes `src/render/shaders/generated/{model,present}_{vert,frag}.h`: a 4-byte
aligned SPIR-V byte array and the MSL source (entry point `main0`).

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

### GT3 lighting (primitives with a valid `scene::Material`)

The VU formula (docs/formats/car_model.md): Me + La·Ma + Σ Ld·Md·max(N·L, 0) +
Σ Ls·Ms·max(N·H, 0)^p, times the packet scale (1 or 255/128), clamped to
255/128. Textured: MODULATE (texel × lit, 0x80 = 1.0). Untextured: the GS
shows the lit byte directly (255 = 1.0), so the shader multiplies by 128/255.
As on the VU, the formula is evaluated per vertex in `model.vert` (lights and
material in vertex uniform slot 1, `VertexLighting`), with GT3's infinite
viewer (H from the view's z axis, not the direction to each vertex), clamped per
vertex to 0..255/128 and interpolated across the triangle like GS Gouraud shading:
linearly in screen space (`noperspective`). Highlights are as soft as the original's on its
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
b' = 255 b, fog amount 1 − F / 255, interpolated in screen space.
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
combination appears mid-race (~200 ms on a Moto G Power 2024, Imagination BXM). On phones and
tablets `GpuContext::warm_pipelines` creates the ~70 combinations races and menus use on a
background thread at start-up (about 5 s on the A15, ~14 s on the Moto, while the opening movie
and title play): the frame's own combinations, then the same for transparent pictures (coverage:
car models on the race's gap pop-ups and results bar, the online pickers' cars), whose first
mid-race pop-up otherwise stalled the Moto for 0.5 s. Others are still created on first use, and
the log names each one ("3D pipeline built on first use"). Mali Bifrost GPUs (G31, G51, G52,
G71, G72, G76, by Vulkan device name: `mali_bifrost`) warm up on the render thread instead: a
Mali-G52 driver (Android 11) crashed in its shader compiler during the background warm-up,
likely compiling on both threads at once (SDL's 2D renderer builds its own pipelines on first
draw). `warm_step` builds one pipeline after each presented frame from start-up (the menus lag
for a few seconds, ~0.1 s a frame), and a race's load (`finish_warming`) builds whatever is
left, so races never compile. On Android the compiling thread is named after the key while
the driver works ("pipe" + blend, cull, depth write, lines, coverage, alpha, "x" MSAA, e.g.
`pipe100000x4`), so a crash report names the pipeline. The start-up log line "3D renderer: GPU
…, driver …" gives the device name and driver version. There are eight samplers: the
`clamp_u`/`clamp_v` combinations (otherwise repeat), each with mipmaps or without
(`scene::Texture::no_mips`: the full-size image only, no anisotropy; the tyres).
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
| `sort_blended` | blended primitives drawn back to front by mesh distance instead of model order |
| `fog`, `fog_color/start/end/max` | linear distance fog (not on background or helper items); `fog_gt3`: GT3's per-vertex depth fog (see "GT3 lighting") |
| `blend_alpha_ref` | alpha-test threshold for blended `kAlphaTest` primitives |
| `post_grade`; `snapshot`, `dissolve`, `trail` | full-screen passes over the finished picture, as GT3's before its 2D layer: the colour grade (levels, a curve per channel, saturation, in GT3's 8-bit table arithmetic; `post_grade_pixel` is the same on the CPU) and, in replays, a dissolve from a frozen picture and a trail of the last one |

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
