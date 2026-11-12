# Resistance 2 PC port: community beta release notes

**Version:** 0.2.2-beta (GitHub pre-release). **Release date:** _to be filled in on release_.

A native PC port of *Resistance 2* (PS3) built by static recompilation: the game's PowerPC and SPU code is translated to C/C++ and compiled for Windows. Graphics go through RPCS3's Vulkan renderer, built into the game as a library.

> **No game files and no prebuilt game are included.** This release is the source code plus Setup. You need your own legally dumped copy of the game (see *Install*); Setup builds the game on your PC from that copy.

This is a **beta**. The campaign has been played through the opening Iceland levels on one PC (Ryzen 5 5600X, GeForce RTX 5070, Windows 11). Later levels are untested. Please report what you find (see *Reporting problems*).

## 0.2.2-beta (setup hotfix)

- **Fixed: the build could fail right away with a Ninja/CMake error** ("build.ninja still dirty", or CMake re-running in a loop) for players in time zones behind ours. Files from the download could be dated a few hours in the future. The installer and zips now carry correct times, and the build resets any future-dated files before it starts.

If your setup failed this way: install 0.2.2 (or wait a few hours) and run Setup again; it continues where it stopped.

## 0.2.1-beta (setup hotfix)

Setup fixes only; the game itself is unchanged from 0.2.0.

- **Progress bar during the build.** The bar no longer stops partway while the game compiles; it follows the build to the end, so Setup no longer looks stuck.
- **Setup shows that it is working:** a running clock for each step, the game copy in GB with the time left, the time left while the game compiles, and a "still working" note during steps that print nothing for several minutes.
- **The installer now opens the Setup window** (with the progress bar and your game already filled in) instead of a console window. Click **Install** there.
- **Much smaller build logs.** The compiler was set to report every possible warning, which made `logs\build.log` grow past 150 MB and slowed the build. It now reports normal warnings only.
- **Low-RAM PCs:** if the build runs out of memory, Setup again detects it and retries with fewer parallel jobs (this check could not read the build log in 0.2.0).

If 0.2.0 is already built and runs, you don't need this update.

## What's in this beta

### Launcher
- `Launcher.exe` holds all settings and key bindings. **Auto-detect** recommends settings for your PC.
- Pages: Display, Graphics, Upscaling & Frame Gen, Performance, Controls, Audio, Interface & Notifications, Advanced, **Trophies**, **Saves & tools** and About (`Launcher.exe --page N` opens one, 0–10).
- The update check only opens the Releases page. Turn it off on the Advanced page or with `PORT_NO_UPDATE_CHECK=1`.

### Graphics
- **Upscaling and anti-aliasing:** NVIDIA DLSS on GeForce RTX cards, AMD FSR 3.1 on any GPU, or Off (`RSX_FSR` = 2 / 1 / 0). If DLSS can't start, the port uses FSR 3.1.
- **Intel XeSS 2 (experimental):** pick *Intel XeSS* in the launcher (Compatible renderer; `RSX_FSR=4`). It runs on any GPU. If it can't start, or on the native renderer, the port uses FSR 3.1. Setup downloads the XeSS SDK after you accept Intel's licence.
- **Render resolution** up to 3840x2160 (`RSX_RES_SCALE`), plus an upscaling ratio (`RSX_UPSCALE`) and sharpening.
- **Frame rate targets:** **60** (default), 30, 120, 144, *Match display*, *Just under refresh* (VRR) and Uncapped (`R2_FPS`).
  - Physics runs at a fixed 60 Hz step.
  - Game speed stays correct at every rate.
- **VRR / G-Sync fix:** VSync *Auto* and *Adaptive* now present with FIFO or FIFO relaxed instead of MAILBOX, which G-Sync / FreeSync doesn't follow. The *Presentation mode* option (`R2_PRESENT=immediate|mailbox|fifo`) overrides this.
- **Refresh rate switch:** in exclusive fullscreen, the display switches to the refresh rate that best fits the frame rate cap (`PORT_REFRESH_SWITCH=1`, experimental, off by default: every switch blanks the screen and can glitch audio).
- **Ultrawide** 21:9 and 32:9 (`R2_ASPECT`), with an optional centred 16:9 HUD (experimental).
- **Shader precompile:** with *Compile shaders before playing* (default on), the cached pipelines are built before the first frame, with a **Compiling shaders N / M** panel. Before Play, the launcher also runs the offline shader scan (shaderscan) with its own progress panel. Shader caches are built on your PC and never shipped.
- **Frame Generation** (experimental, off by default; Upscaling & Frame Gen page, `R2_DLSS_FG`, `R2_FG_BACKEND=auto|dlss|fsr3`): NVIDIA DLSS Frame Generation 2x on an RTX 40, up to 4x on an RTX 50 (needs NVIDIA DLSS); other GPUs get AMD FSR 3 2x.
- **Optional extras:**
  - HDR output (`R2_HDR`)
  - NVIDIA Reflex (`R2_REFLEX`)
  - toggles for screen blur, camera shake and cinematic black bars
  - colour-blind filters

### Controls
- **Mouse and keyboard:** raw mouse look and rebindable keys (`controls.txt`, `mouselook.cfg`).
- **Controllers through SDL3** (the default; `PAD_BACKEND=xinput` is still available): Xbox, PlayStation, Switch Pro and others. Optional gyro aiming, light bar and Nintendo button layout.
- **Button prompts follow your device** (`R2_PAD_GLYPHS=auto|xbox|ps|keys`): keyboard keys, or Xbox / PlayStation buttons.
- **Local split-screen co-op.**

### PC features
- **PC menu:** F10, or hold View + Menu. It has Resume, PC Options and **Quit to Desktop** (with confirmation; it waits for a save in progress to finish).
- **PC wording:** text that refers to the console is changed to PC terms (`R2_PC_TEXT=0` turns this off).
- **Screenshots** with F12 or PrtScn. **F3** toggles the performance overlay.
- **Menus, overlay and notifications are drawn into the game image** (`PORT_OSD`), so they also show in exclusive fullscreen. The movie-skip hint can be turned off (*Show cutscene skip hint*, `PORT_SKIP_HINT=0`).
- Alt+Enter toggles windowed mode. You can choose the monitor, and pause or mute when the game is in the background.
- **Trophies** are recorded locally. There is a **mods folder**, and **crash / hang reports** go to `logs\crash\`.

### Audio
- **Time stretching** (SoundTouch, `PORT_AUDIO_STRETCH=1`, default on): when the frame rate drops, audio slows slightly instead of crackling or gapping.

### Diagnostics
- **Hitch logger** (`PORT_HITCH_LOG=1`, default on; `PORT_HITCH_MS`, default 25 ms): each slow frame writes one `[hitch]` line to `logs\run.log`.

## What you download

- **`Resistance2Port-Setup-0.2.0-beta.exe`** from the GitHub release: an installer with the port's source, scripts and `Launcher.exe`. It installs the files and starts Setup.
- or the **source code** (the release's source archive or `git clone` at the tag), then `Setup.bat`.

Either way, the game itself is built on your PC; no built game program is published. Setup downloads the build tools, RPCS3's source, FFmpeg (an LGPL build), shaderscan and, if you accept their licences, the NVIDIA DLSS, Streamline (DLSS Frame Generation) and Intel XeSS SDKs.

## Requirements

- Windows 10 or 11, 64-bit.
- A CPU with **AVX2** (Intel 4th generation or newer, or any AMD Ryzen).
- A **Vulkan 1.3** GPU. DLSS needs a GeForce RTX card. FSR works on AMD, NVIDIA and Intel. Only NVIDIA has been tested so far.
- The Microsoft Visual C++ 2015–2022 x64 runtime, version 14.40 or newer.
- Your own copy of **Resistance 2, EU disc (BCES00226), version 1.00**. Other regions, digital copies and patched versions are not supported yet.
- About **35 GB** of free disk space: about 22 GB for the game files plus the build and the shader cache.
- For setup: an internet connection and the free build tools. Setup installs the tools itself.

## Install

1. **Dump your own game.** Use a decrypted ISO, or a game folder containing `PS3_GAME` (the same kind of folder RPCS3 uses), from your own disc.
2. **Install the port.** Either:
   - run `Resistance2Port-Setup-<version>.exe`, which installs to `C:\Games\Resistance2Port` by default and asks for your game; or
   - download the source code (the release's source archive, or `git clone` at the release tag) into a folder without spaces and double-click `Setup.bat`.
3. **Setup** finds or asks for your game (`Setup.bat -Game "<ISO or folder>"` skips the question). It then:
   - checks the copy and copies it to `gamefiles\PS3_GAME`;
   - decrypts `EBOOT.BIN` to `game\EBOOT.elf` with the public keys from RPCS3's source;
   - downloads RPCS3's source at a fixed version and applies the port's patch;
   - generates and builds the game-specific code. This takes 20–60 minutes the first time.
4. **Play** with `Launcher.exe`.

**How the port finds your game:**
- `run.bat`, which the launcher starts, runs `build\Resistance2.exe` with `game\EBOOT.elf` and points the game's virtual disc at `gamefiles` (`PS3_VFS_ROOT`).
- Saves go in `gamedata\dev_hdd0\home\00000001\savedata` (`PS3_GAMEDATA_ROOT` moves them).
- Settings and controls are also kept in `%USERPROFILE%\Saved Games\Resistance 2\config`. An empty `portable.txt` turns that off.
- The shader cache backup is in `%LOCALAPPDATA%\Resistance2Port\shadercache`.

**First launch:** before the game starts, the launcher prepares the shader cache from your game files (a progress panel; **Skip for now** or Esc skips it). The game then compiles its known shaders behind a **Compiling shaders N / M** panel. Later starts are fast.

## Known issues

- Occasional freezes. One cause, an SPU job jumping to an empty command slot, has a workaround; there may be others.
- Above 60 fps, physics still updates at 60 Hz without interpolation, so motion can look slightly juddery.
- Lighting and bloom can look brighter in some spots at 60 fps (also reported on RPCS3). If it bothers you, use 30 fps.
- With DLSS, blob shadows under moving pickups can trail a little, and some foliage and wire fences can still shimmer.
- **Ambient occlusion is disabled with DLSS** in this beta (also when DLSS falls back to FSR 3.1). It still works with FSR or no upscaler.
- Heavy fights are CPU-bound (high 30s in the biggest ones on a mid-range CPU). Frame Generation (launcher → Upscaling & Frame Gen) can help here.
- **Frame Generation is experimental and barely tested** (DLSS on RTX 40/50, FSR 3 elsewhere; off by default). Expect ghosting and HUD or menu artifacts; reports are welcome.
- **Changing Frame Generation in-game** can double the sound and then leave constant static. Change it in the launcher, or restart the game after changing it.
- "open FAIL" lines in the log for streaming files the EU disc doesn't have are harmless.
- Exclusive fullscreen can give black F12 screenshots, and the screen goes black briefly at start and when switching windows. Borderless avoids both.
- Everything else, including levels past the opening Iceland levels: **TBD after testing**.
- What's planned next: [ROADMAP.md](ROADMAP.md).

## Reporting problems

Please read **[REPORTING.md](REPORTING.md)**. Include how to reproduce the problem, the session's log from `logs\` (the launcher has a button for it), and any report from `logs\crash\`.

## Licences

The port is MIT-licensed (see `LICENSE`). Its RPCS3 patch is GPL-2.0, and the game you build links RPCS3, so that build is GPL-2.0; don't redistribute it with the DLSS files. See `THIRD_PARTY_NOTICES.md`. This port uses NVIDIA DLSS. *Resistance 2* is a trademark of its owners. This project is not affiliated with or endorsed by them.
