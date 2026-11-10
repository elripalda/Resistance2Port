# Resistance 2 — native PC port/recomp (work in progress)

A static recompilation of the PS3 game *Resistance 2* into a native Windows program. No emulation of the CPU: the game's PowerPC and SPU code is translated to C/C++ and compiled for your PC. Graphics go through RPCS3's Vulkan renderer, with NVIDIA DLSS / AMD FSR upscaling, 60 fps, keyboard and mouse controls, and higher resolutions.

> ⚠️ **Not Perfect.** I have only tested it **on my PC** (Ryzen 5 5600X, GeForce RTX 5070, 32 GB RAM, Windows 11, 1080p screen) and haven't beaten the game. The campaign has been played through the opening Iceland levels; past that it's untested, so things could go wrong. Some crashes or freezes may happen.

> **This repository contains no game files and no game code.** You need your own copy of the game. The setup builds everything game-specific on your PC from that copy.

## What you need

- A PC running Windows 10 or 11 (64-bit).
- A CPU with AVX2: Intel 4th gen or newer, or any AMD Ryzen. Older CPUs without AVX2 won't run it.
- A Vulkan 1.3 GPU. DLSS needs a GeForce RTX card; everything else (including FSR) works on AMD, NVIDIA and Intel. Only tested on NVIDIA so far.
- Your own dump of **Resistance 2, EU disc (BCES00226), version 1.00**. This is the folder that contains `PS3_GAME`, the same kind of folder RPCS3 uses. Other regions and patched versions are not supported yet.
- About 35 GB of free disk space: ~22 GB for the game files and the rest for the build tools and the build.
- An internet connection for the setup.

## Install

1. Download this repository: click on Code at the top, then Download ZIP, then unzip it somewhere with no spaces in the path, for example `C:\Games\Resistance2Port`.
2. Double-click **`Setup.bat`**. It will:
   - install the free build tools if they're missing (Visual Studio 2022 Build Tools, LLVM, CMake, Ninja, Python, Git), using `winget`;
   - ask you to pick your game folder, then copy it into the port folder (or move it, if it's on the same drive);
   - decrypt the game's program file, using the application keys from RPCS3's public source code;
   - download RPCS3's source at a fixed version and apply this port's renderer patch (`ps3recomp/patches/r2-rpcs3.patch`);
   - generate the game code from your copy and build the game.

   The first run takes a while: tool installs plus a 20–60 minute build. At the end, Setup offers to put a "Resistance 2" shortcut to the launcher on your desktop.
3. Play with **`Launcher.exe`**. It has the settings (display mode, resolution, frame rate, VSync, anti-aliasing, upscaling, texture filtering, sharpening) and the key bindings.

You only need to run Setup again after downloading a newer version of the port; otherwise just use the launcher.

**First launch:** the game shows its own one-time setup screens and an autosave notice. The first time you load into a level, expect some stutter while the graphics shaders are compiled; after that, shaders are cached.

## Controls

Keyboard and mouse are supported out of the box (WASD, mouse look, Mouse1 fire, Mouse2 aim, R reload, G grenade, E use, V/Mouse4 melee, Q / wheel switch weapon, T flashlight, M map, Esc pause). Controllers work too. All keyboard and mouse bindings can be changed in the launcher.

**Button prompts follow the device you used last.** Touch the keyboard or mouse and the on-screen prompts (hints, menu legends, tutorial text) show your keys; press a button or move a stick on a controller and they switch to that controller's buttons. Switching is instant and works mid-game. With an Xbox / XInput controller (or any non-PlayStation pad) the prompts show Xbox buttons, drawn by the port as simple coloured letters (no copied artwork):

| PS3 | Xbox | PS3 | Xbox |
|---|---|---|---|
| Cross | A | L1 / R1 | LB / RB |
| Circle | B | L2 / R2 | LT / RT |
| Square | X | L3 / R3 | LS / RS (stick click) |
| Triangle | Y | Select / Start | View / Menu |
| D-pad | D-pad | Left / right stick | Left / right stick |

If a Sony DualShock 3/4 or DualSense is plugged in (detected by its USB/Bluetooth ID, used through DS4Windows / Steam Input as XInput) the PlayStation symbols are kept. The launcher's **Controller button prompts** option forces Xbox or PlayStation. Prompts name the game's default button layout; if you remap buttons in the game's own options, the prompts still name the default ones.

**Mouse look is direct:** raw mouse counts turn the camera's yaw and pitch each frame (× the launcher's sensitivity × the game's own Look Sensitivity), not an emulated right stick — no acceleration, dead zone or speed cap. While aiming down sights the turn rate is scaled by the scope zoom so the same mouse movement covers the same part of the screen (`R2_ZOOM_SENS`: 1 = default, 0 = no scaling, up to 2). The game's aim assist only acts on stick input, so it doesn't pull against the mouse, and while the mouse is in use the right stick's turn rate is ignored so the two never add up. Mouse counts made while the camera isn't yours (menus, pause, cutscenes, scripted sequences) are dropped, so the view doesn't jump afterwards. The launcher's **Mouse aiming = Classic** switches back to stick emulation. The game has no mouse pointer in its menus; they are driven by the keyboard (arrows / Enter / Esc) or a controller. F8 frees the cursor.

**Controllers can be plugged in and out at any time** (XInput, up to four); no restart. An empty controller slot is re-checked about once a second.

## PC features

- **PC menu (Resume / PC Options / Quit to Desktop):** press **F10**, or hold **View + Menu** (Select + Start) on a controller for 1.5 s — from gameplay, the game's pause menu or the main menu. The game is paused and gets no input while it is open. It always opens on **Resume**; the mouse takes over the highlight only once it is moved. Navigate with arrows / W S A D, Enter / Space / A = OK, Esc / Backspace / B = back, F10 = close; the mouse works too (hover, left click = OK / next value, right click = previous value). A controller's d-pad or left stick moves the selection.
  - **Quit to Desktop** asks *Quit to desktop?* — Enter / Space / A = yes, Esc / Backspace / B = back to the PC menu. On yes (and also when the window is closed with Alt+F4 or the X button) the port waits for a save in progress to finish (up to 15 s, "Saving..."), flushes open files and exits.
  - **PC Options**: *Frame rate* (30 / 60 / 120 / 144 / Match display / Uncapped), *Upscaler / anti-aliasing* (Off / AMD FSR 3.1 / NVIDIA DLSS), *Mouse sensitivity* (0.10–4.00, steps of 0.05), *Field of view* (Original / Wider / Widest — the Extended FOV patch below, can misalign some shadows), *Button prompts* (Auto / Keyboard keys / Xbox / PlayStation). Mouse sensitivity, field of view and button prompts apply at once; frame rate and upscaler are marked `*` and apply the next time the game starts. Every change is saved straight to the launcher's files (`settings.ini`, `settings.bat`, `controls.txt`), so the launcher shows it too.
  - **Hint:** when you open the game's own pause menu (Esc / Enter / Start during play) a small box at the bottom says *PC menu: F10 or hold View + Menu*, and the same box shows once ~12 s after start-up (for the main menu). `R2_MENU_PC_HINT=0` hides it (it is off by default with exclusive fullscreen).
  - `R2_MENU_PC=0` turns the PC menu off: F10 / View+Menu then open only the old *Quit to desktop?* box.
  - The entries are an overlay drawn by the port, not rows inside the game's Scaleform menus (see *Not done* below).
- **PC wording in the game's text:** English (US / UK) strings that talk about the wireless controller, the PS button, the PS3 hard disk or "PLAYSTATION" installs are rewritten as the text files load (e.g. *Press and hold the PS button…* → *Press F10 to quit the game.*, *Your HDD has insufficient space.* → *Insufficient disk space.*). Only replacements that fit in the original string are made. Other languages are left as they are. `R2_PC_TEXT=0` turns it off.
- **Alt+Enter** switches between borderless fullscreen and windowed. **Alt+Tab** is safe in borderless and windowed mode.
- **Monitor** (launcher, Display › PC): which screen the window opens on (`RSX_MONITOR=<n>`, 1-based in Windows' order; empty = primary).
- **When the game is in the background** (launcher): *Pause and mute* (default) freezes the game and silences it when the window loses focus; *Mute only*; *Keep running* (`R2_BACKGROUND=pause|mute|run`). While paused (this way or by the PC menu) the SPU and RSX threads idle instead of spinning a core each. On focus loss the cursor is always released and shown; it is shown while the quit box is up or the game is paused this way, and captured/hidden again when you click back in.
- **Skip intro logo** (launcher, default on): skips the Insomniac logo video by pressing Start/Cross for the game during it (`R2_SKIP_INTRO=0` to watch it).
- **Screenshots:** **F12** or **PrtScn** saves `screenshots\R2_<date>_<time>.png` in the game folder (a short "Screenshot saved" note appears). The launcher's About page has **Open screenshots**.
- **Saves** are in the game folder: `gamedata\dev_hdd0\home\00000001\savedata\BCES00226_SAVE_0`. Back them up by copying that folder. **Open save folder** (launcher, About) opens it. `PS3_GAMEDATA_ROOT=<dir>` moves the whole `gamedata` tree elsewhere (saves then go to `<dir>\dev_hdd0\home\00000001\savedata`). Settings are `settings.ini` / `settings.bat` and `controls.txt` in the game folder.
- **Reset all to defaults** (launcher, About) resets display, graphics, PC options, keys and mouse (saves untouched). **Auto-detect** (Display/Graphics) resets only display and graphics; **Restore default controls** only the controls.
- These features (F10 PC menu, F12, pause on focus loss, monitor choice) are in the Compatible (RPCS3) and Native Vulkan renderers' window. Exclusive fullscreen may hide the PC menu / quit box and give black screenshots; use borderless.

### All launcher options

| Page | Option | Values (default first) | Environment variable |
|---|---|---|---|
| Display | Display mode | Borderless, Exclusive fullscreen, Windowed | `RSX_DISPLAY` |
| Display | Window size | 1280x720 … 3840x2160 (1920x1080) | `RSX_WINDOW` |
| Display | Frame rate | 60, 30, 120, 144, Match display, Just under refresh (VRR), Uncapped | `R2_FPS` (`vrr`) |
| Display | VSync | Off, On, Adaptive, Auto | `RSX_VSYNC` |
| Display | CPU profile | Auto, Low, Mid, High | `PORT_CPU_PROFILE` |
| Display › PC | Monitor | Primary, Monitor 1…n | `RSX_MONITOR` |
| Display › PC | When the game is in the background | Pause and mute, Mute only, Keep running | `R2_BACKGROUND` |
| Display › PC | Skip intro logo | On, Off | `R2_SKIP_INTRO` |
| Display › PC | Controller button prompts | Auto, Xbox, PlayStation, Keyboard keys | `R2_PAD_GLYPHS` (`auto`/`xbox`/`ps`/`keys`) |
| Display › PC | Ultrawide / aspect ratio | 16:9, Fit window, 21:9, 32:9 | `R2_ASPECT` |
| Display › PC | Presentation mode | Default, Immediate (VRR), Mailbox, FIFO | `R2_PRESENT` |
| Display › PC | Performance overlay | Off (F3 shows it), On at start, Disabled | `R2_OVERLAY` |
| Display › PC | Trophy notifications | On, Off | `R2_TROPHY_TOAST` |
| Display › PC | Mods folder | Off, On | `R2_MODS` |
| Graphics › Graphics quality | Field of view | Original, Wider (0.85), Widest (0.7) | `R2_PATCH_FOV_VALUE` |
| Graphics | Render resolution, Scaling, Sharpening, Anti-aliasing, Upscaling, Texture filtering, Renderer, Shader compilation, and the Graphics/Renderer quality options | see the Graphics page help | `RSX_RES_SCALE`, `RSX_FILTER`, `RSX_SHARPEN`, `RSX_FSR`, `RSX_UPSCALE`, `RSX_ANISO`, `R2_RSX`, `R2_SHADER_MODE`, `R2_PATCH_*` |
| Controls | Keys (two per action), Mouse sensitivity, Invert vertical look, Mouse aiming (Direct / Classic), Free the mouse cursor key (F8) | | `controls.txt` |
| Controls › Aiming and sticks | Vertical mouse sensitivity, Aiming sensitivity, Aim mode, Stick deadzone, Stick response curve | see *Options* below | `PAD_MOUSE_SENS_Y`, `R2_ADS_SENS`, `PAD_AIM_TOGGLE`, `PAD_DEADZONE`, `PAD_CURVE` |
| Trophies | The game's trophy list and what you have earned | | |

Other switches (set them in `settings.bat` yourself): `R2_KEY_PROMPTS=0` turns off all prompt replacement (PS3 symbols always), `R2_MENU_PC=0` / `R2_MENU_PC_HINT=0` / `R2_PC_TEXT=0` (above), `R2_ZOOM_SENS` (above), `PS3_GAMEDATA_ROOT` (above).

**Not done, and why:** rows inside the game's own menus (the main and pause menus are Scaleform Flash movies, `frontend.swf` / `pausemenu.swf`, driven by the PS3 menu code; adding a row means patching ActionScript and the code behind it blind, without running the game — so the PC menu is an overlay opened with F10 / View+Menu, and a hint appears with the pause menu instead); a frame cap for menus (the port has no reliable "in a menu" signal); a mouse pointer in menus (the game has no pointer support); moving saves to `%APPDATA%` (would orphan existing saves — use `PS3_GAMEDATA_ROOT` if you want that); native DualSense/DualShock input without an XInput wrapper (only the button symbols detect them).

## Options

PC-port options added on top of the PS3 game. Each one is an environment variable that the launcher writes to `settings.bat` (set it there yourself, or in the launcher; unset = the default shown first). The overlay toggle is also exported for the in-game PC menu (`r2pf_overlay_set`).

**Display**

- **Ultrawide / arbitrary aspect** — `R2_ASPECT` = *(unset)* / `off` / `16:9`: the PS3 16:9 picture, pillarboxed on wider screens (default). `auto` (launcher *Fit window*): the 3D view takes the window's shape, re-read live when the window is resized. `21:9` (= 64:27), `32:9`, `W:H`, `WxH` or a plain ratio (`2.4`) force one. The port widens the game's camera frustum Hor+ (horizontal FOV grows, vertical FOV stays as on PS3; the aspect handed to the projection follows) by hooking the game's two frustum setters, and the RPCS3 renderer then stretches the frame to the window instead of letting it in a 16:9 box. The HUD and menus are stretched to the width: the game draws them in the same 16:9 render target as the 3D scene, and they could not be told apart reliably enough to keep them in a centred 16:9 region (so `R2_HUD_MODE=center` is not available). Only the main 3D camera is widened; with a narrower than 16:9 window (e.g. 4:3) the view is Vert-.
- **Presentation mode** — `R2_PRESENT` = `immediate` (also `vrr`, `tearing`): never wait for vblank (best with G-Sync / FreeSync); `mailbox`: newest frame each vblank, no tearing (falls back to FIFO-relaxed); `fifo` (or `vsync`): classic VSync. Unset follows the VSync setting (`RSX_VSYNC`). Compatible (RPCS3) renderer only; the Native Vulkan renderer keeps `RSX_VSYNC`.
- **Frame rate just under refresh** — `R2_FPS=vrr`: caps at refresh − refresh²/3600 (≈ 57.1 on 60 Hz, 138 on 144 Hz, 228 on 240 Hz) so a VRR display never leaves its range. Pair it with *Presentation mode = Immediate* or VSync off.
- Exclusive fullscreen / borderless / windowed, monitor choice and Alt+Enter: see *PC features* above.

**Performance overlay** — **F3** in game shows or hides a small box in the top-left corner: game fps and frame time (average and worst of the last 0.5 s), and on the Compatible renderer the RSX (GPU-side) frame rate, draws per frame and average µs of render-thread time per draw. `R2_OVERLAY=1` shows it at start, `R2_OVERLAY=0` disables F3; `R2_OVERLAY_POS=tl|tr|bl|br` picks the corner. It is a separate layered window, so it is not visible in exclusive fullscreen; while it is shown the renderer's profiling counters run (a small cost; `R2_RSX_PROF=1` also prints them to the log every 5 s).

**Trophies → local achievements** — every trophy the game awards is recorded (the runtime's `gamedata\trophies\NPWR00291.json`, plus a readable copy in `%USERPROFILE%\Saved Games\Resistance 2\trophies.json` with names, grades and unlock times). The names come from the game's own `TROPHY.TRP` in `gamefiles\PS3_GAME\TROPDIR` (read at run time; nothing is shipped). A notification slides in at the top-right of the game window for 5 s (`R2_TROPHY_TOAST=0` turns it off; same layered-window caveat as the overlay). The platinum is awarded once every trophy it counts is earned. The launcher's **Trophies** page lists all of them (hidden ones stay hidden until earned).

**Input** (launcher: Controls › Aiming and sticks)

- Key / mouse-button rebinding: Controls page (`controls.txt`).
- `PAD_MOUSE_SENS_Y=<0.1..4>` — vertical mouse sensitivity relative to horizontal (direct and classic mouse aiming).
- `R2_ADS_SENS=<0.1..3>` — mouse sensitivity while aiming down sights / scoped, relative to hip fire (on top of `R2_ZOOM_SENS`).
- `PAD_AIM_TOGGLE=1` — Aim (L1 / Mouse2 / LB) toggles instead of hold, for keyboard and controller; menus and the pause screen release it.
- `PAD_DEADZONE=<0..0.9>` — controller stick deadzone, radial (unset: the XInput standard 24 % per axis, as before).
- `PAD_CURVE=<0.5..3>` — stick response exponent past the deadzone (1 linear, >1 finer near the centre, <1 faster).
- Invert vertical look: Controls page (`controls.txt`). DualShock 4 / DualSense: through Steam Input or DS4Windows as an XInput controller (the port has no SDL input backend, so there is no native DualSense support).

**Mods folder** — `R2_MODS=1` (launcher: Mods folder = On) loads files from `mods\` next to the game (`R2_MODS=<folder>` names another folder). Each sub-folder is one mod laid out like the game's `PS3_GAME` folder: `mods\MyMod\USRDIR\<path of the file to replace>`. Mods are applied in alphabetical order (a later one wins on the same file); a folder name starting with `_` or `-` is skipped. Every file layer of the runtime (cellFs, sys_fs) uses the replacement. `logs\run.log` lists each mod and override at start-up (`[mods] override: ...`) and the first use of each (`[mods] using ...`). Only whole game files can be replaced; there is no per-texture replacement (the game's textures live inside packed level files, and the RPCS3 texture cache has no cheap hash-and-replace point).

**Shaders** — the Compatible renderer keeps every GPU pipeline it builds in `shadercache\rpcs3\` and, at start-up, builds all of them again before the first frame (RPCS3's shader cache preload), so after the first play-through nothing is compiled in game. `R2_SHADER_CACHE=0` turns the disk cache off; `R2_SHADER_MODE` (launcher: Shader compilation) picks asynchronous / interpreter / synchronous compilation for pipelines that are not in the cache yet. There is no separate first-launch warm-up: the pipelines depend on the game's state and cannot be generated before they are first seen.

**Not done:** HDR output (the game produces an 8-bit SDR image; an HDR swapchain would only re-encode it, so a real HDR mode needs the game's tone-mapping rewritten); frame generation (needs motion vectors and a separate HUD the port does not have — DLSS/FSR here are used for upscaling / anti-aliasing only); a centred 16:9 HUD at ultrawide (above).

## Graphics options

- **Frame rate:** 60 fps by default (the game was 30 fps on PS3). The game's own frame limiter is replaced by a precise PC-side one, the same idea as RPCS3's "Unlock FPS" patch. 30 fps is still available in the launcher.
- **Display mode:** borderless fullscreen, exclusive fullscreen or windowed, with VSync off / on / adaptive.
- **Anti-aliasing → NVIDIA DLSS** uses DLSS on GeForce RTX cards and falls back to FSR 3.1 on other GPUs. The DLSS SDK is downloaded by Setup into `tools\dlss` (if that fails, the port still builds without it).
- **Upscaling** (with FSR or DLSS on): Native (DLAA), Quality, Balanced or Performance. The default is DLSS Performance at a 4K render resolution, which is then downscaled to your screen: sharper than native at about the cost of native. Menus and HUD stay at full resolution.
- Moving objects (like rotating weapon pickups) are masked for DLSS so they don't leave ghost copies.
- **Low-end PCs:** the launcher's **Recommended for this PC** button picks settings for your CPU and GPU. Tuned for a 4-core CPU, a GTX 1050-class GPU and 8 GB of RAM; on 4 cores the first level holds 60 fps. On CPUs with fewer than 8 threads the port stops raising thread priorities and pinning threads to cores, and its busy-wait loops give the CPU to other threads. If a 4-core PC still stutters, use 30 fps. Advanced: `PS3_PRIO_BOOST=0/1` forces priority boosts off or on; `PORT_MAIN_CPU=<n>` pins the main thread to CPU n.
- **Shader-program re-sends** (renderer, on by default): the game sends the same shader programs again hundreds of times per frame. A re-sent fragment program with the same code only has its constants refreshed (`R2_FP_REBIND=0` turns this off), a vertex program uploaded or selected again with the same code keeps its analysis (`R2_VP_REBIND=0`), and when only render state changed and the previous draw's pipeline still matches it is reused without a cache lookup (`R2_PIPE_MEMO=0`). `R2_RSX_PROF=1` shows them as *re-sent FP/VP kept* and *pipeline memo hits*. All renderer switches are listed in `ps3recomp/third_party/rpcs3/README.port.md`.
- **ZCULL labels over a flip** (`R2_ZCULL_FLIP_LAG=<n>`, experimental, **default 0 = off**): in strict ZCULL mode, lets occlusion labels still waiting on the GPU be carried over up to `n` flips instead of waiting at every flip. They are also released whenever the RSX command queue runs dry. It hung Resistance 3 in one bench, so leave it off unless you are testing it.
- **CPU profile** (Display page): Auto picks from the cores the game can use (logged as `[cpu]`). Low (≤4 cores) turns off the timing spin-waits, folds the Vulkan submit thread into the render thread and uses fewer shader-compiler threads. Mid (6 cores) shortens the spin-waits. High changes nothing. The port also spreads its main, RSX and FIFO threads over separate cores, keeps them on P-cores on hybrid Intel CPUs and lowers background threads. Advanced: `PORT_CPU_PROFILE=auto|low|mid|high|off` (env settings you set yourself always win); `PORT_THREAD_PLACEMENT=0` turns off thread placement.

## RPCS3 patches

The community RPCS3 patches for Resistance 2 (RPCS3's official `patch.yml`, also mirrored in illusionyy/rpcs3-game-patch) are ported here. The PowerPC code is recompiled, so byte patches to instructions become hooks (`tools/hooks/r2.json`), and data patches are written into game memory at boot (`project/r2_patches.cpp`), only if the original value is there (EBOOT 1.00, PPU hash `08da3b601b335c5b09e6425b27fe00eacdad6e78`).

| Patch (author) | What it does | Guest address(es) | How it's ported | Setting | Default |
|---|---|---|---|---|---|
| Unlock FPS (Whatcookie): wait removal | Removes the frame limiter's wait | `0x00635D7C`, `0x00635D88` (nop) | Hook in `func_00635D28` (`r2_frame_period`, `project/stubs.cpp`): the game's own wait is bypassed and frames are paced on the PC side | `R2_FPS=30` / `<Hz>` / `refresh` / `uncapped` | 60 (launcher) |
| Unlock FPS (Whatcookie): frame-time floors | The game clock's minimum frame time: 1/30 s initial value, 1/75 s lower clamp → 0.0001 | `0x00D529D8`, `0x00D69EB8` (float) | Data patch at boot | Automatic when `R2_FPS` ≠ 30; `R2_PATCH_FPS_FLOORS=1` also applies it at 30 fps | follows `R2_FPS` |
| Extended FOV (illusion) | Wider field of view. RPCS3's note: breaks shadows (misaligned, jumping when the player moves) | `0x00DE67E4` (float 1.0 → 0.7) | Data patch at boot | `R2_PATCH_FOV=1`; `R2_PATCH_FOV_VALUE=0.3..1.0` (lower = wider; setting it alone also turns the patch on; launcher / PC menu *Field of view*, live in the PC menu) | off |
| Port's own timing fix (not an RPCS3 patch) | Frame counts that assume 30 Hz are scaled by fps/30, so they keep their real-time length at 60+ fps: a 150-frame deadline, and once-a-second / every-2-seconds periodic checks | `func_0017F8EC`, `func_005140A8`, `func_0052BE60`, `func_0057CDEC`, `func_00599620` | Hooks (`r2_timing_frames`, `project/r2_patches.cpp`) | `R2_PATCH_TIMING_FRAMECOUNT=0` turns it off | on (no effect at 30 fps) |
| Port's own: native scene resolution | Turns off the game's dynamic resolution (the 3D scene drops from 1280x704 to 1152x640 on frames the game thinks are GPU-heavy). Same effect as the game's debug argument `-no_dynares` | `0x00DF7FDC` (word 1 → 0) | Data patch at boot | `R2_PATCH_FULLRES=1` | off |
| Port's own: no load shedding | The same governor also shortens LOD distances (x0.75) and trims effect budgets on GPU-heavy frames; this turns the whole GPU-time path off (implies `R2_PATCH_FULLRES`) | `0x00D52604` (float 2000.0 → 1e20, read only by `func_002C3780`) | Data patch at boot | `R2_PATCH_NO_LOADSHED=1` | off |
| Port's own: LOD distance | Multiplies the LOD transition/fade distances | `func_001721C8` (`0x00172450`) | Hook (`r2_lod_scale`, `project/r2_patches.cpp`) | `R2_PATCH_LOD_SCALE=1.0..2.0` | 1.0 (off) |

Not ported: the 1.60 and PSN (NPEA00431) versions of the same patches (different EBOOT), and the Artemis collection's "Infinite health" / "Endless ammo" for BCUS98120 1.60 (cheats, wrong version). No community patch exists for this game for motion blur, depth of field, film grain, shadow resolution, LOD or SPU load.

## Graphics enhancements

Beyond DLSS / FSR, all optional and off by default (launcher: Graphics page, "GRAPHICS QUALITY"). Each logs a `[patch]` line at boot.

- **Native scene resolution** (`R2_PATCH_FULLRES=1`). Resistance 2 has dynamic resolution: its render targets are 1280x704, and each frame a governor (`func_002C3780`) reads the previous frame's GPU time from RSX timestamps; when it is over budget it draws that frame's 3D scene at 1152x640 inside the same targets, and the final pass stretches it 10/9 onto the 1280x720 display. This patch keeps every frame at 1280x704 (the targets' full size, so nothing can be cut off; the remaining 704 → 720 stretch is 2%). With `RSX_RES_SCALE=3` the scene is 3840x2112 instead of 3456x1920; `RSX_RES_SCALE=1.5` gives a 1920x1056 scene for 1080p → DLSS 4K. Rendering a true 1280x720 scene was not done: the 704-line targets are allocated at boot in a fixed video-memory layout shared by several systems, and the gain over 704 is under 3%.
- **No load shedding** (`R2_PATCH_NO_LOADSHED=1`). Disables the governor's GPU-time path entirely: no resolution drop, no LOD-distance cut (x0.75) and no effect-budget cuts (x0.5 / x0.25 / x0.05) on slow frames. The CPU-time path is left as is.
- **LOD distance** (`R2_PATCH_LOD_SCALE=1.25 / 1.5 / 2`). Detailed models stay in use further away and distant objects fade out later. The multiplier is the one the game itself lowers to 0.75 under load; values above 1 are beyond what the game was tuned for, so watch for pop-in changes and frame time.
- Renderer-side switches (see the bridge): `RSX_ANISO`, `R2_LOD_BIAS`, `R2_HIGHP_Z`, `R2_MSAA`, `R2_GPU_TEX_SCALING`, sharpening.

Not found statically with enough confidence to patch: shadow-map resolution (render targets are allocated once at boot; the shadow sizes were not identified), motion blur, depth of field and film grain toggles, particle/decal limits. The game's own debug arguments (`-noeffects`, `-nodecals`, `-shadows` …) only remove things or enable debug views.

## Status

This is a work in progress, tested on a single PC.

- Runs at a steady 60 fps in most places on my PC; it can dip to the high 30s in heavy areas and during level streaming.
- Performance is mostly limited by the game's SPU job system, so a decent CPU helps most.
- Performance fixes have been applied as far as I realistically could, on the CPU and render-thread side. In the first heli level (1080p internal, DLSS to 4K), the render thread went from about 30 fps to an average of about 54 fps (it was 30 because it waited on ZCULL occlusion queries). It now spends about 9 µs per draw at about 1,250 draws per frame. ZCULL reads no longer stall (`R2_ZCULL=sync` restores the strict behaviour). The game's own main PPU thread and SPU jobs are now the limit.
- The renderer recovers from a corrupt RSX command stream instead of freezing (it logs `FIFO: recovering` to `logs\rpcs3_rsx.log`).
- There are occasional freezes and other bugs.

If you hit a problem, let me know how to reproduce it, and if you can, send the log from the `logs\` folder for that session (the launcher has a button for it). That helps a ton.

## How it works

- `ps3recomp/` is a modified copy of [ps3recomp](https://github.com/sp00nznet/ps3recomp) by sp00nznet (MIT licence). It holds the runtime, the PS3 system libraries (audio, input, file system, SPU scheduling) and the lifters that translate PowerPC and SPU code into C/C++.
- Graphics: the game's RSX command stream is fed to RPCS3's RSX core and Vulkan renderer, built as a library. This repository only contains the patch to RPCS3 (`ps3recomp/patches/r2-rpcs3.patch`); Setup downloads the original source and applies it. The patch adds DLSS / FSR upscaling, display modes and fixes for running outside the emulator.
- `project/` is the game-specific part of the port: its hooks, the 60 fps limiter, mouse look, the launcher and the DLSS bridge.
- `tools/r2_regen.py` runs the lifters on your decrypted `EBOOT.elf`, then re-applies this port's own edits to the generated code (`tools/hooks/`).

## Known Bugs

- Occasional freezes. One cause (an SPU job jumping to an empty command slot, which showed up more often at 60 fps) has a workaround; there may be others.
- 60 fps and timing:
  - The game's own clock runs on real frame time, clamped to 0.08 s per frame.
  - Physics runs at a fixed 60 Hz step and carries the remainder over, so its speed is correct at any frame rate. Above 60 fps it still updates at 60 Hz, with no interpolation, so it can look slightly juddery.
  - Cutscene and keyframe timers convert 30 fps key frames to seconds, so they keep their speed.
  - The few frame-count timers that assume 30 Hz are scaled; see `R2_PATCH_TIMING_FRAMECOUNT` above.
  - Many AI and level-of-detail updates are staggered over N frames, so they run more often at 60 fps. That costs CPU but doesn't change game speed.
  - RPCS3 users report brighter lighting/bloom in some spots at 60 fps. The game's PowerPC code has no exposure or eye-adaptation logic, so the cause is probably in SPU or GPU-side lighting. It isn't fixed. If it bothers you, use 30 fps in the launcher.
- With DLSS, blob shadows under moving pickups can still trail a little.
- Some streaming sound/dialogue files that the EU disc doesn't have show "open FAIL" lines in the log. That's harmless.

## Credits and licences

- [Resistance: Fall of Man PC port](https://github.com/Psyykosis/Resistance-Fall-of-Man-Recomp): this port builds on its runtime, tools and research (SPURS/SPU job handling, frame pacing, input, audio and video fixes).
- ps3recomp, by Ned Heller / sp00nznet: MIT.
- RPCS3 (RSX core and Vulkan renderer), downloaded during setup and patched: GPL-2.0. Because the built game links RPCS3 code, **the built program is distributed under the GPL-2.0**; the patch in this repository is GPL-2.0 too.
- AMD FidelityFX SDK (FSR 3.1): MIT, © Advanced Micro Devices.
- NVIDIA DLSS SDK v310.9.16, downloaded during setup into `tools/dlss/`: NVIDIA RTX SDK licence (`tools/dlss/LICENSE.txt`), not part of this repository.
- FFmpeg 7.1.1 (gyan.dev build), downloaded during setup for the game's videos: GPL/LGPL, not part of this repository.
- RPCS3's `key_vault.cpp`, downloaded during setup and used only to decrypt your own copy.
- The port's own code: MIT, see `LICENSE`.

*Resistance 2* is © Sony Interactive Entertainment, developed by Insomniac Games. This is an unofficial fan project, not affiliated with or endorsed by Sony or Insomniac. Please own the game you play.
