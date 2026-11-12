# Resistance 2 — native PC port/recomp (work in progress)

<a href="https://recomplauncher.com/game/resistance2port"><picture><source media="(prefers-color-scheme: dark)" srcset="https://recomplauncher.com/assets/press/launcher/badge-light.svg"><img alt="Get it on RecompLauncher" src="https://recomplauncher.com/assets/press/launcher/badge-dark.svg" height="48"></picture></a>

A static recompilation of the PS3 game *Resistance 2* into a native Windows program. No emulation of the CPU: the game's PowerPC and SPU code is translated to C/C++ and compiled for your PC. Graphics go through RPCS3's Vulkan renderer, with NVIDIA DLSS / AMD FSR upscaling, 60 fps, keyboard and mouse controls, and higher resolutions.

> ⚠️ **Not Perfect.** I have only tested it **on my PC** (Ryzen 5 5600X, GeForce RTX 5070, 32 GB RAM, Windows 11, 1080p screen) and haven't beaten the game. The campaign has been played through the opening Iceland levels; past that it's untested, so things could go wrong. Some crashes or freezes may happen.

> **No game files and no translated game code are distributed, in this repository or in the installer.** You need your own copy of the game. Setup decrypts its program on your PC, translates it into C/C++ and builds the game there; the generated code and the built game never leave your PC, and no prebuilt game program is released. The repository holds the port's own code plus the small references it needs to find its place in the game's program: function addresses, patch values and short context lines from the generated code (`tools/hooks/` and `project/r2_patches.cpp`).

## What you need

- A PC running Windows 10 or 11 (64-bit).
- A CPU with AVX2: Intel 4th gen or newer, or any AMD Ryzen. Older CPUs without AVX2 won't run it.
- A Vulkan 1.3 GPU. DLSS needs a GeForce RTX card; everything else (including FSR) works on AMD, NVIDIA and Intel. Only tested on NVIDIA so far.
- Your own dump of **Resistance 2, EU disc (BCES00226), version 1.00**. This is the folder that contains `PS3_GAME`, the same kind of folder RPCS3 uses. Other regions and patched versions are not supported yet.
- About 35 GB of free disk space: ~22 GB for the game files and the rest for the build tools and the build.
- An internet connection for the setup.

## Install

**Quick install (recommended):**

1. Download the release zip from the Releases page and unzip all of it to a folder with no spaces in the path, for example `C:\Games\Resistance2Port`.
2. Run **`Setup.exe`** in that folder. (It is not code-signed: if Windows says *"Windows protected your PC"*, click **More info → Run anyway**.)
3. Pick your game: **ISO...** or **Folder...**. Setup checks the copy at once and says what to do if it can't be used (see *If your game copy is not accepted* below).
4. Click **Install** and wait. Setup installs the missing build tools (one administrator prompt), copies your game files, builds the game with as many parallel jobs as your RAM allows, adds Start-menu and desktop shortcuts and starts the launcher. It shows the progress and the time left. If it stops (error, sleep, power cut), run `Setup.exe` again and click **Resume**.

The release contains no game code: the game is always built from your own copy. Requirements, disk space, build times, uninstalling and troubleshooting (SmartScreen, antivirus, low RAM, no winget): [docs/INSTALL.md](docs/INSTALL.md).

### Manual install (advanced)

**With the Inno installer** (if a `-Setup-<version>.exe` is on the Releases page): download `Resistance2Port-Setup-<version>.exe` from the Releases page and run it. It installs the port (default `C:\Games\Resistance2Port`), adds Start-menu and desktop shortcuts, and asks for your game. It lists the copies it finds on the PC (common folders and RPCS3's game list); you can also type or paste a path, or use the **ISO...** / **Folder...** buttons. It checks the copy before going on and says what is wrong and what to do if it can't be used (see *If your game copy is not accepted* below). At the end it opens **Setup**, the window with the progress bar, with your game already filled in: click **Install** there (step 2 below). The installer contains only the port; the game files always come from your own copy. Use a decrypted ISO or a game folder (the same kind RPCS3 uses). Tick **Portable mode** to keep settings only in the install folder.

**By hand:**

1. Download this repository: click on Code at the top, then Download ZIP, then unzip it somewhere with no spaces in the path, for example `C:\Games\Resistance2Port`.
2. Double-click **`Setup.bat`**. It will:
   - install the free build tools if they're missing (Visual Studio 2022 Build Tools, LLVM, CMake, Ninja, Python, Git), using `winget`;
   - look for your game (an ISO or a game folder), check it, then copy it into the port folder (or move a folder, if it's on the same drive);
   - decrypt the game's program file, using the application keys from RPCS3's public source code;
   - download RPCS3's source at a fixed version and apply this port's renderer patch (`ps3recomp/patches/r2-rpcs3.patch`);
   - generate the game code from your copy and build the game.

   The first run takes a while: tool installs plus a 20–60 minute build. At the end, Setup offers to put a "Resistance 2" shortcut to the launcher on your desktop.
3. Play with **`Launcher.exe`**. It has the settings (display mode, resolution, frame rate, VSync, anti-aliasing, upscaling, texture filtering, sharpening) and the key bindings.

You only need to run Setup again after downloading a newer version of the port; otherwise just use the launcher.

**Choosing the game in Setup.bat:** it lists the copies it found, numbered. Type a number, or type or paste the path of the ISO or game folder (you can also drag it into the window). **B** opens a file picker (Windows only). `Setup.bat -Game "<path>"` skips the question. The path can be an ISO, the game folder, or a file inside it. An ISO is opened by Windows, copied from and closed again; if Windows can't open it, Setup reads it directly.

### If your game copy is not accepted

The installer and Setup check the copy before copying anything. The supported version is **Resistance 2 (EU disc, BCES00226)**, about 23.3 GB.

| Message | What to do |
|---|---|
| compressed archive (.zip, .7z, .rar) | Extract it first, then pick the ISO or folder inside. |
| PlayStation Store package (.pkg) / game update folder | Pick the disc copy. Game updates are not needed. |
| not &lt;game&gt; / from another region | Use the supported disc version listed above. |
| PlayStation Store version | Only the disc version is supported. |
| encrypted disc image / PARAM.SFO is not valid | Decrypt the ISO first (for example PS3Dec with the disc key from the `.dkey` file), or use a game folder from a PS3 or RPCS3. |
| program file was decrypted or replaced (plain ELF) | Use the original, unmodified files. Setup decrypts them itself. |
| files are missing / smaller than expected | The copy is incomplete. Copy or dump it again. |
| different from the known file (warning) | The copy may be modified. You can continue; Setup stops later if the decrypted program is not the supported version. |
| not enough free space | Free up space, or install on another drive. |

The checks run on your PC only. Nothing is uploaded.

**Updates:** the launcher checks this repository's Releases page once at start and says when a newer version is out (it only opens the download page; nothing is downloaded or replaced). Turn it off with the launcher's *Check for updates* option (Advanced page) or the environment variable `PORT_NO_UPDATE_CHECK=1`. The installed version is in the `VERSION` file.

**Settings and controls** (`settings.ini`, `settings.bat`, `controls.txt`, `mouselook.cfg`) are also kept in `%USERPROFILE%\Saved Games\Resistance 2\config`. Missing files are restored from there when the launcher or game starts, so they survive reinstalling or updating the port. To keep everything in the port folder only, create an empty file named `portable.txt` next to `Launcher.exe` (the installer's *Portable mode* does this). Saves are still kept in the port folder (`gamedata`); the uninstaller asks before deleting it, and its default is to keep it.

**No console window:** the launcher starts the game without a console. The game's output goes to `logs\run.log`.

**First launch:** the game shows its own one-time setup screens and an autosave notice. Before the game starts, the launcher prepares the shader cache from your game files (a progress panel; **Skip for now** or Esc skips it), and the game then compiles every known shader behind a **Compiling shaders N / M** panel (see *Shader precompile*), so levels play without shader stutter from the first visit. Shaders are cached for later starts.

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

With the XInput backend (`PAD_BACKEND=xinput`), a Sony DualShock 3/4 or DualSense that is plugged in (detected by its USB/Bluetooth ID, used through DS4Windows / Steam Input as XInput) keeps the PlayStation symbols; with SDL3 (default) the prompts follow the controller type, see *Controllers (SDL3)* below. The launcher's **Controller button prompts** option forces Xbox or PlayStation. Prompts name the game's default button layout; if you remap buttons in the game's own options, the prompts still name the default ones.

**Mouse look is direct:** raw mouse counts turn the camera's yaw and pitch each frame (× the launcher's sensitivity × the game's own Look Sensitivity), not an emulated right stick — no acceleration, dead zone or speed cap. While aiming down sights the turn rate is scaled by the scope zoom so the same mouse movement covers the same part of the screen (`R2_ZOOM_SENS`: 1 = default, 0 = no scaling, up to 2). The game's aim assist only acts on stick input, so it doesn't pull against the mouse, and while the mouse is in use the right stick's turn rate is ignored so the two never add up. Mouse counts made while the camera isn't yours (menus, pause, cutscenes, scripted sequences) are dropped, so the view doesn't jump afterwards. The launcher's **Mouse aiming = Classic** switches back to stick emulation. The game has no mouse pointer in its menus; they are driven by the keyboard (arrows / Enter / Esc) or a controller. F8 frees the cursor.

**Controllers (SDL3).** The port reads controllers through SDL3 (built in, no extra DLL): Xbox, DualShock 4, DualSense, DualShock 3 (with its Windows driver), Switch Pro and Joy-Con work directly. **DS4Windows and Steam Input are no longer needed** (they still work; if they expose a virtual Xbox pad next to the real controller, the port ignores the duplicate so it doesn't become a ghost player 2). Controllers can be plugged in and out at any time, no restart, up to 8. Rumble works on every pad that has it (DualSense included), and a DualShock 4 / DualSense light bar shows the player's colour (player 1 blue, player 2 red). If SDL3 can't start, the port falls back to XInput automatically (`logs\run.log` shows `[cellPad] controllers: SDL3` or `SDL3 could not start`).

**Button prompts by controller type:** with *Controller button prompts = Auto*, a PlayStation pad (DualShock 3/4, DualSense) shows PlayStation symbols and an Xbox or other pad shows Xbox letters. Switch controllers are read by button position (the bottom button is Cross); with *Nintendo button layout = Nintendo style* A (right) confirms and B cancels as on a Switch, and the prompts show the Xbox-style letters. Under Steam Input the real controller type is used when SDL reports it; otherwise set the prompts by hand.

**Gyro aiming** (DualShock 4, DualSense, Switch Pro, Joy-Con; off by default): turning the controller turns the view, added directly to the camera like the mouse (smooth, not limited by the stick). *While aiming* only uses it with the Aim button (L1 / LB) held. Leave the controller still for about a second after it connects so the resting drift is calibrated. In split-screen co-op each player's gyro moves only their own view (as a right-stick input, like the second player's mouse).

| Option (launcher Controls page / in game *More PC Options*) | Values | Variable |
|---|---|---|
| Controller support | **SDL3**, XInput | `PAD_BACKEND=sdl3\|xinput` (restart). `sdl2` is accepted and uses SDL3 |
| Gyro aiming | **Off**, On, While aiming | `PAD_GYRO=0\|1\|aim` |
| Gyro sensitivity | **2x**, 0.5x-6x | `PAD_GYRO_SENS=<0.05..20>` (view degrees per controller degree) |
| Gyro vertical | **Normal**, Inverted | `PAD_GYRO_INVERT_Y=1` (on top of the game's own invert setting) |
| Gyro turning axis | **Controller**, Player | `PAD_GYRO_SPACE=local\|player` (player = yaw around gravity, works flat or upright) |
| Gyro calibration | **On connect**, Off | `PAD_GYRO_CALIBRATE=0` turns off the 1 s still-calibration |
| Light bar | **Player colour**, Off | `PAD_LIGHTBAR=0` |
| Nintendo button layout | **By position**, Nintendo style | `PAD_NINTENDO_SWAP=1` |
| (no launcher option) | **On** | `PAD_DEDUP=0` keeps virtual duplicates (Steam Input / DS4Windows / ViGEm) as separate controllers |
| (no launcher option) | **360** | `PAD_GYRO_STICK_DPS=<deg/s>`: gyro speed that equals a full right stick (co-op / classic mouse aiming, where gyro drives the stick) |

With `PAD_BACKEND=xinput` the old behaviour returns: XInput only (up to four), PlayStation pads through DS4Windows / Steam Input, no gyro or light bar.

## Local co-op

Two players on one PC, split-screen, as on PS3. No PSN account is needed: the port provides a second local user.

**Starting it:** plug in two controllers (or one, with *Player 2 uses keyboard and mouse*). Main menu → **Co-op** → the offline / split-screen co-op staging screen (the online options need PSN and don't work). On the staging screen player 2 presses **START** (or Enter / Esc on the keyboard for a keyboard player 2) to join: on PS3 this opens the "select a user" box; the port answers it at once with a local user named **Player 2**, and player 2 shows as logged in. Start the mission and the screen splits.

Only the offline, split-screen part of co-op works: everything that needs PSN (online co-op, competitive, the lobby browser) stays unavailable.

**Who is who:**

- Player 1 = the keyboard and mouse plus the first controller found (as in single player).
- Player 2 = the next controller. A controller keeps its player while it stays plugged in. A controller plugged in later takes the lowest free player; when one is unplugged, its player has no input until it is plugged back.
- With two local players the direct mouse look is off. The mouse then moves the keyboard player's view like a right stick (it would otherwise turn both heroes).

**Options** (launcher, Controls page; or `settings.bat`):

| Option | Values | Variable |
|---|---|---|
| Local co-op (split-screen) | **On**, Off | `PAD_COOP=0`: the game sees only player 1. Extra controllers (or duplicate virtual pads from Steam Input / DS4Windows) can't join |
| Player 1 controller | **Automatic**, Controller 1-8 | `PAD_P1_CONTROLLER=<1..8>`: the controller that is player 1 (SDL3: in the order they were connected; XInput: 1-4, Windows' XInput order) |
| Player 2 controller | **Automatic**, Controller 1-8 | `PAD_P2_CONTROLLER=<1..8>`: the controller that is player 2 (numbered as above) |
| Player 2 uses keyboard and mouse | **Off**, On | `PAD_P2_KEYBOARD=1`: player 1 = the first controller only, player 2 = keyboard and mouse |
| (no launcher option) | **2** | `PS3_LOCAL_USERS=<1..4>`: local users the game sees. `1` = player 2 can't sign in. `PS3_USER2_NAME=<name>` renames player 2 |

`logs\run.log` shows which controller each player got (`[cellPad] player 2 <- controller 2`) and when the game counts two local players (`[coop] local players: 2`).

**Known limits:** Player 2's progress and settings are not saved separately (as on PS3 for a guest player). If you use NVIDIA DLSS, it may not work right in split-screen. Turn it off (or use FSR / no upscaler) for co-op if the image smears or one half looks wrong.

## PC features

- **PC menu (Resume / PC Options / Quit to Desktop):** press **F10**, or hold **View + Menu** (Select + Start) on a controller for 1.5 s — from gameplay, the game's pause menu or the main menu. The game is paused and gets no input while it is open. It always opens on **Resume**; the mouse takes over the highlight only once it is moved. Navigate with arrows / W S A D, Enter / Space / A = OK, Esc / Backspace / B = back, F10 = close; the mouse works too (hover, left click = OK / next value, right click = previous value). A controller's d-pad or left stick moves the selection.
  - **Quit to Desktop** asks *Quit to desktop?* — Enter / Space / A = yes, Esc / Backspace / B = back to the PC menu. On yes (and also when the window is closed with Alt+F4 or the X button) the port waits for a save in progress to finish (up to 15 s, "Saving..."), flushes open files and exits.
  - **PC Options**: *Frame rate* (30 / 60 / 120 / 144 / Match display / Uncapped), *Upscaler / anti-aliasing* (Off / AMD FSR 3.1 / NVIDIA DLSS), *Mouse sensitivity* (0.10–4.00, steps of 0.05), *Field of view* (Original / Wider / Widest — the Extended FOV patch below, can misalign some shadows), *Button prompts* (Auto / Keyboard keys / Xbox / PlayStation). Mouse sensitivity, field of view and button prompts apply at once; frame rate and upscaler are marked `*` and apply the next time the game starts. Every change is saved straight to the launcher's files (`settings.ini`, `settings.bat`, `controls.txt`), so the launcher shows it too.
  - **Hint:** when you open the game's own pause menu (Esc / Enter / Start during play) a small box at the bottom says *PC menu: F10 or hold View + Menu*, and the same box shows once ~12 s after start-up (for the main menu). `R2_MENU_PC_HINT=0` hides it (it is off by default with exclusive fullscreen).
  - `R2_MENU_PC=0` turns the PC menu off: F10 / View+Menu then open only the old *Quit to desktop?* box.
  - The entries are an overlay drawn by the port, not rows inside the game's Scaleform menus (see *Not done* below).
- **PC wording in the game's text:** strings that talk about the console are rewritten as the text files load (rules in `project\r2_pctext.cpp`). In English: the health warning (*…see the WARNING section in the Safety and Support Manual included with your PS3™ system* → *Before playing, please read the health and safety warnings for this game.*), the auto-save and save-icon notices (*…do not quit the game or turn off your PC*), *Press and hold the PS button…* → *Press F10 to quit the game.*, HDD wording → disk wording, *PlayStation®Network* → *online service* / *online account*, *PSN Friends* → *Friends*, *wireless controller* → *controller* / mouse, *PLAYSTATION®3* / *PS3™ system* → *PC*, the XrossMediaBar cable help → *your PC's display settings*. Other languages: *PLAYSTATION®3* / *PS3* → *PC*, *PlayStation®Network* → *Online*, *PlayStation®Store* → *Store*, the PS button in quit messages → F10, and *SIXAXIS™* / *PSN* are dropped. Credits and trademark lines are left as they are. Only replacements that fit in the original string are made, and only the text in memory changes (the game files aren't touched). `R2_PC_TEXT=0` turns it off.
- **Alt+Enter** switches between borderless fullscreen and windowed. **Alt+Tab** is safe in borderless and windowed mode.
- **Monitor** (launcher, Display › Screen): which screen the window opens on (`RSX_MONITOR=<n>`, 1-based in Windows' order; empty = primary).
- **When the game is in the background** (launcher): *Pause and mute* (default) freezes the game and silences it when the window loses focus; *Mute only*; *Keep running* (`R2_BACKGROUND=pause|mute|run`). While paused (this way or by the PC menu) the SPU and RSX threads idle instead of spinning a core each. On focus loss the cursor is always released and shown; it is shown while the quit box is up or the game is paused this way, and captured/hidden again when you click back in.
- **Skip intro logo** (launcher, default on): skips the Insomniac logo video by pressing Start/Cross for the game during it (`R2_SKIP_INTRO=0` to watch it).
- **Screenshots:** **F12** or **PrtScn** saves `screenshots\R2_<date>_<time>.png` in the game folder (a short "Screenshot saved" note appears). The launcher's About page has **Open screenshots**.
- **Saves** are in the game folder: `gamedata\dev_hdd0\home\00000001\savedata\BCES00226_SAVE_0`. The launcher's **Saves & tools** page lists them and backs them up (see *PC extras* below). **Open save folder** (launcher, About) opens it. `PS3_GAMEDATA_ROOT=<dir>` moves the whole `gamedata` tree elsewhere (saves then go to `<dir>\dev_hdd0\home\00000001\savedata`). Settings are `settings.ini` / `settings.bat` and `controls.txt` in the game folder.
- **Reset all to defaults** (launcher, About) resets display, graphics, PC options, keys and mouse (saves untouched). **Auto-detect** (Display, Graphics and Upscaling & Frame Gen pages) resets only display and graphics; **Restore default controls** only the controls.
- These features (F10 PC menu, F12, pause on focus loss, monitor choice) are in the Compatible (RPCS3) and Native Vulkan renderers' window. The port's menus, overlay and notifications are drawn into the game's own image (`PORT_OSD`, default), so they also show in exclusive fullscreen; `PORT_OSD=layered` uses the old separate windows, which exclusive fullscreen hides. Exclusive fullscreen can still give black screenshots; borderless is recommended.

### All launcher options

The options are sorted into pages, each with group headings (the same pages as the Resistance: Fall of Man launcher). `Launcher.exe --page N` opens on page N: 0 Display, 1 Graphics, 2 Upscaling & Frame Gen, 3 Performance, 4 Controls, 5 Audio, 6 Interface & Notifications, 7 Advanced, 8 Trophies, 9 Saves & tools, 10 About. The Display, Graphics and Upscaling & Frame Gen pages start with the *Recommended for this PC* panel; **Rebuild shader cache** is on the Performance page. Any option no group lists would appear on the Advanced page under *Other*.

| Page | Option | Values (default first) | Environment variable |
|---|---|---|---|
| Display › Screen | Display mode | Borderless, Exclusive fullscreen, Windowed | `RSX_DISPLAY` |
| Display › Screen | Window size | 1280x720 … 3840x2160 (1920x1080) | `RSX_WINDOW` |
| Display › Frame rate | Frame rate | 60, 30, 120, 144, Match display, Just under refresh (VRR), Uncapped | `R2_FPS` (`vrr`) |
| Display › Frame rate | VSync | Off, On, Adaptive (FIFO relaxed), Auto (On when capped at or under the refresh) | `RSX_VSYNC` |
| Display › Screen | Monitor | Primary, Monitor 1…n | `RSX_MONITOR` |
| Display › Screen | Ultrawide / aspect ratio | 16:9, Fit window, 21:9, 32:9 | `R2_ASPECT` |
| Display › Frame rate | Presentation mode | Default, Immediate (VRR), Mailbox, FIFO | `R2_PRESENT` |
| Display › Frame rate | Refresh rate switch (exclusive fullscreen) | **Off**, On | `PORT_REFRESH_SWITCH` (`1` = on, experimental) |
| Display › Screen | Resizable window / Alt+Enter | On, Off | `RSX_RESIZABLE` / `RSX_ALT_ENTER` (`0` = off) |
| Display › HDR | HDR output / paper white / peak | Off, HDR10, scRGB / 200, 100…300 nits / 1000, 400…1500 nits | `R2_HDR` (`1`/`scrgb`), `R2_HDR_PAPER`, `R2_HDR_PEAK` |
| Display › Screen | Ultrawide HUD | Stretched, Centred 16:9 (experimental) | `RSX_HUD_FIX` |
| Graphics › Effects and accessibility | Screen blur / Camera shake / Cinematic black bars | On, Off | `R2_FX_SCREEN_BLUR` / `R2_FX_CAMERA_SHAKE` / `R2_FX_LETTERBOX` (`0` = off) |
| Graphics › Effects and accessibility | Colour-blind filter | Off, Protanopia, Deuteranopia, Tritanopia | `R2_COLORBLIND` (`1`/`2`/`3`) |
| Graphics › View | Field of view | Original, Wider (0.85), Widest (0.7) | `R2_PATCH_FOV_VALUE` |
| Graphics › Quality | Level of detail distance | Original, 1.25x, 1.5x, 2x, 3x, 4x | `R2_PATCH_LOD_SCALE` |
| Graphics › Quality | Shadow resolution | Original, 2x, 4x | `RSX_SHADOW_SCALE` |
| Graphics › Quality | Effects resolution | Original, Full, Full (all) | `RSX_FX_FULLRES` |
| Graphics › Quality | Ambient occlusion | Off, On (experimental) | `RSX_SSAO`. Ambient occlusion is disabled with DLSS in this beta (`RSX_SSAO_DLSS=1` forces it on). |
| Graphics › Resolution | Render resolution, Scaling | see the Graphics page help | `RSX_RES_SCALE`, `RSX_FILTER` |
| Graphics › Quality | Texture filtering, and the other quality options | see the Graphics page help | `RSX_ANISO`, `R2_PATCH_*` |
| Graphics › Renderer | Renderer | see the Graphics page help | `R2_RSX` |
| Upscaling & Frame Gen › Frame generation and latency | NVIDIA Reflex | Off, On, On + Boost | `R2_REFLEX` (`1`/`2`) |
| Upscaling & Frame Gen › Frame generation and latency | Frame Generation (RTX 40/50: DLSS-G, needs NVIDIA DLSS; other GPUs: FSR 3 2x) | **Off**, 2x, 3x, 4x, FSR 3 2x (only what the GPU supports) | `R2_DLSS_FG` (`2`/`3`/`4`/`fsr3`), `R2_FG_BACKEND` |
| Upscaling & Frame Gen › Anti-aliasing and upscaling | Anti-aliasing, Upscaling, Sharpening | see the Upscaling & Frame Gen page help | `RSX_FSR`, `RSX_UPSCALE`, `RSX_SHARPEN` |
| Performance › Processor | CPU profile | Auto, Low, Mid, High | `PORT_CPU_PROFILE` |
| Performance › Shader cache | Shader compilation | see the Performance page help | `R2_SHADER_MODE` |
| Controls › Controller | Controller button prompts | Auto, Xbox, PlayStation, Keyboard keys | `R2_PAD_GLYPHS` (`auto`/`xbox`/`ps`/`keys`) |
| Controls › Aiming and sticks | Extra crosshair | Off, Dot, Cross | `R2_CROSSHAIR` (`dot`/`cross`) |
| Controls | Keys (two per action), Mouse sensitivity, Invert vertical look, Mouse aiming (Direct / Classic), Free the mouse cursor key (F8) | | `controls.txt` |
| Controls › Aiming and sticks | Vertical mouse sensitivity, Aiming sensitivity, Aim mode, Stick deadzone, Stick response curve | see *Options* below | `PAD_MOUSE_SENS_Y`, `R2_ADS_SENS`, `PAD_AIM_TOGGLE`, `PAD_DEADZONE`, `PAD_CURVE` |
| Controls › Controller, Gyro | Controller support, Gyro aiming, Gyro sensitivity, Gyro vertical, Gyro turning axis, Gyro calibration, Light bar, Nintendo button layout | see *Controllers (SDL3)* under Controls | `PAD_BACKEND`, `PAD_GYRO`, `PAD_GYRO_SENS`, `PAD_GYRO_INVERT_Y`, `PAD_GYRO_SPACE`, `PAD_GYRO_CALIBRATE`, `PAD_LIGHTBAR`, `PAD_NINTENDO_SWAP` |
| Controls › Controller | Pause when the controller disconnects | On, Off | `R2_PAD_DISCONNECT_PAUSE` (`0` = off) |
| Controls › Controller | Swap sticks (southpaw) | Off, On | `PAD_SOUTHPAW=1` |
| Controls › Controller buttons | Button A … Menu: what each button does | Default, Acts as … | `PAD_MAP_A` … `PAD_MAP_START` (`A B X Y LB RB LT RT LS RS BACK START`) |
| Audio › In the background | When the game is in the background | Pause and mute, Mute only, Keep running | `R2_BACKGROUND` |
| Audio › Sound | Speaker layout | **5.1 surround**, 7.1 surround, Stereo | `PORT_SPEAKERS` (`8` / `2`) |
| Audio › Sound | Audio buffer | **Auto** (100 ms; 150 ms under 12 CPU threads), 80, 100, 150, 200 ms | `PORT_AUDIO_BUFFER_MS` |
| Interface & Notifications › Overlays | Performance overlay | Off (F3 shows it), On at start, Disabled | `R2_OVERLAY` |
| Interface & Notifications › Notifications | Trophy notifications | On, Off | `R2_TROPHY_TOAST` |
| Interface & Notifications › Photo mode and Discord | Discord Rich Presence | Off, On | `R2_DISCORD=1` (+ `R2_DISCORD_APPID` or `discord_appid.txt`) |
| Interface & Notifications › Photo mode, Movies | Photo mode (F7) / Hold to skip movies | On, Off | `R2_PHOTO` / `R2_CUTSCENE_SKIP` (`0` = off) |
| Interface & Notifications › Movies | Show cutscene skip hint | **On**, Off | `PORT_SKIP_HINT` (`0` = off; `R2_SKIP_HINT` also read) |
| Advanced › Start-up | Skip intro logo | On, Off | `R2_SKIP_INTRO` |
| Advanced › Mods and textures | Mods folder | Off, On | `R2_MODS` |
| Advanced › Saves | Back up saves before playing | On (keep 10), keep 5, keep 30, Off | `R2_SAVE_BACKUP` (`5`/`30`/`0`; read by the launcher) |
| Advanced › Mods and textures | Texture pack / Dump textures | Off, On | `RSX_TEXPACK` / `RSX_TEXDUMP` |
| Trophies | Progress summary (earned / total, %, per grade), every trophy with its icon, grade and unlock date | | read-only: `TROPHY.TRP`, `gamedata\trophies\NPWR00291.json`, `Saved Games\Resistance 2\trophies.json` |
| Saves & tools | Saved games, Back up now, Restore / Delete a backup, Open folders, Run benchmark, Open results | | `save_backups\`, `logs\bench\` |

Other switches (set them in `settings.bat` yourself): `R2_KEY_PROMPTS=0` turns off all prompt replacement (PS3 symbols always), `R2_MENU_PC=0` / `R2_MENU_PC_HINT=0` / `R2_PC_TEXT=0` (above), `R2_ZOOM_SENS` (above), `PS3_GAMEDATA_ROOT` (above).

**Not done, and why:** rows inside the game's own menus (the main and pause menus are Scaleform Flash movies, `frontend.swf` / `pausemenu.swf`, driven by the PS3 menu code; adding a row means patching ActionScript and the code behind it blind, without running the game — so the PC menu is an overlay opened with F10 / View+Menu, and a hint appears with the pause menu instead); a frame cap for menus (the port has no reliable "in a menu" signal); a mouse pointer in menus (the game has no pointer support); moving saves to `%APPDATA%` (would orphan existing saves — use `PS3_GAMEDATA_ROOT` if you want that); native DualSense/DualShock input without an XInput wrapper (only the button symbols detect them).

## Options

PC-port options added on top of the PS3 game. Each one is an environment variable that the launcher writes to `settings.bat` (set it there yourself, or in the launcher; unset = the default shown first). The overlay toggle is also exported for the in-game PC menu (`r2pf_overlay_set`).

**Display**

- **Ultrawide / arbitrary aspect** — `R2_ASPECT` = *(unset)* / `off` / `16:9`: the PS3 16:9 picture, pillarboxed on wider screens (default). `auto` (launcher *Fit window*): the 3D view takes the window's shape, re-read live when the window is resized. `21:9` (= 64:27), `32:9`, `W:H`, `WxH` or a plain ratio (`2.4`) force one. The port widens the game's camera frustum Hor+ (horizontal FOV grows, vertical FOV stays as on PS3; the aspect handed to the projection follows) by hooking the game's two frustum setters, and the RPCS3 renderer then stretches the frame to the window instead of letting it in a 16:9 box. The HUD and menus are stretched to the width: the game draws them in the same 16:9 render target as the 3D scene, and they could not be told apart reliably enough to keep them in a centred 16:9 region (so `R2_HUD_MODE=center` is not available). Only the main 3D camera is widened; with a narrower than 16:9 window (e.g. 4:3) the view is Vert-.
- **Presentation mode** — `R2_PRESENT` = `immediate` (also `vrr`, `tearing`): never wait for vblank (best with G-Sync / FreeSync); `mailbox`: newest frame each vblank, no tearing, but at the fixed refresh, so G-Sync / FreeSync do not follow it (falls back to FIFO-relaxed); `fifo` (or `vsync`): classic VSync. Unset follows the VSync setting (`RSX_VSYNC`). Compatible (RPCS3) renderer only; the Native Vulkan renderer keeps `RSX_VSYNC`.
- **Frame rate just under refresh** — `R2_FPS=vrr`: caps at refresh − refresh²/3600 (≈ 57.1 on 60 Hz, 138 on 144 Hz, 228 on 240 Hz) so a VRR display never leaves its range. Pair it with *Presentation mode = Immediate* or VSync off.
- Exclusive fullscreen / borderless / windowed, monitor choice and Alt+Enter: see *PC features* above.

**Frame pacing, VSync and G-Sync / FreeSync** (Compatible renderer)

- Present mode per setting (the log prints it once: `[display] Vulkan present mode FIFO, 3840 x 2160`): VSync *Off* / `R2_PRESENT=immediate` = IMMEDIATE; *On* / `fifo` = FIFO; *Adaptive* = FIFO relaxed (MAILBOX if the driver has no FIFO relaxed); `R2_PRESENT=mailbox` = MAILBOX. *Auto* (`RSX_VSYNC=auto`) = FIFO whenever the frame rate is capped at or under the display refresh (the cap means it never waits on vblank) and Adaptive when uncapped or above the refresh.
- G-Sync / FreeSync follow FIFO, FIFO relaxed and IMMEDIATE. MAILBOX shows the newest frame at the fixed refresh, so VRR does not follow it: 60 fps on a 144 Hz screen then holds frames for 2, then 3 refreshes (judder) even with G-Sync on. Earlier builds mapped *Auto* and *Adaptive* to MAILBOX; they now use FIFO / FIFO relaxed.
- Without VRR, a fixed-refresh display is only even at a divisor of its rate (60 on 60 / 120 / 240 Hz, 72 or 48 on 144 Hz); the log warns (`[fps] note: ...`). **Refresh rate switch** — `PORT_REFRESH_SWITCH` (default off, `1` = on, experimental: every switch blanks the screen and can glitch audio; launcher *Display › Frame rate › Refresh rate switch*): in exclusive fullscreen (`RSX_DISPLAY=exclusive`) the display is switched to the nearest mode at the same resolution whose refresh is a multiple of the frame rate (`[fps] display switched 144->120 Hz`), and back to the desktop rate when the game loses focus, is minimised, leaves fullscreen, closes or crashes. Borderless and windowed never switch (use G-Sync / FreeSync or an even frame rate there). With VRR the switch is harmless but unnecessary; `0` avoids the mode-change flash.
- With `PORT_OSD=layered`, popup windows over the game (performance overlay, extra crosshair, toasts) make Windows compose the screen, which can turn G-Sync off while they are visible. By default they are drawn into the game image and do not.

**Hitch logger** — `PORT_HITCH_LOG` (default on, `0` = off; `PORT_HITCH_MS=<ms>`, default 25): every game frame over 25 ms (or over 2x the frame-time target) prints one `[hitch]` line with where that frame went (game, limiter, sync waits, pad, RSX flip interval, present / acquire / GPU wait / DLSS, pipeline compiles, texture uploads, audio wait, and `present-iv` = the longest present-to-present interval). Every 5 s: `[hitch] 5 s:` (CPU frame time p50 / p99 / max) and `[hitch] present 5 s:` (present-to-present interval p50 / p99 / max, `uneven` = intervals more than 4 ms off the median, and how many frames a fixed-refresh display would hold for 1 / 2 / 3 / 4+ refreshes; a mix of 2 and 3 = judder unless VRR is active). Since this build the `[hitch]` line also splits `guestwait` (the game thread's own guest waits) into spin / sleep / lwmutex / lwcond and names the guest call site that waited longest (`top 0x... X ms`), shows the game thread's CPU time (`game X (cpu Y)`: a gap = the thread was preempted), and breaks down the RSX thread: `fifo-idle` (waiting for commands), `label ms/n` (NV406E semaphore waits), `zcull ms/n` (occlusion report reads waiting for the GPU), `hard-sync ms/n`, `submits n` and `rsx-limit` (RPCS3 flip limiter sleep). Hot-thread log lines (`[pipe]`, `[fps]`, `[hitch]`) are queued to a writer thread (`PORT_LOG_SYNC=1` writes them at once).

**Frame pacing** — one pacer: when the frame rate is not 30, the game limiter (`R2_FPS`) paces the frame loop and the RPCS3 flip limiter is off (it was a second 60 Hz clock: the RSX thread slept in the flip while the game waited on it, then the next frame ran at once). `GCM_FRAME_LIMIT=vblank` restores the old double pacing.

**Performance overlay** — **F3** in game shows or hides a small box in the top-left corner: game fps and frame time (average and worst of the last 0.5 s), and on the Compatible renderer the RSX (GPU-side) frame rate, draws per frame and average µs of render-thread time per draw. `R2_OVERLAY=1` shows it at start, `R2_OVERLAY=0` disables F3; `R2_OVERLAY_POS=tl|tr|bl|br` picks the corner. It is drawn into the game's own image (`PORT_OSD`), so it also shows in exclusive fullscreen; while it is shown the renderer's profiling counters run (a small cost; `R2_RSX_PROF=1` also prints them to the log every 5 s).

**Trophies → local achievements** — every trophy the game awards is recorded (the runtime's `gamedata\trophies\NPWR00291.json`, plus a readable copy in `%USERPROFILE%\Saved Games\Resistance 2\trophies.json` with names, grades and unlock times). The names come from the game's own `TROPHY.TRP` in `gamefiles\PS3_GAME\TROPDIR` (read at run time; nothing is shipped). A notification slides in at the top-right of the game window for 5 s (`R2_TROPHY_TOAST=0` turns it off; drawn into the game image like the overlay). The platinum is awarded once every trophy it counts is earned. The launcher's **Trophies** page shows a summary at the top (earned / total, percentage, a progress bar and the count per grade), then every trophy with its icon from `TROPHY.TRP` (greyed until earned), name, description, grade and the date it was earned, newest first. Hidden trophies show as *Hidden trophy* until earned. It merges both records (a trophy counts if either has it; the earliest time wins), so the platinum the port awards also shows. Before anything is earned it says *Play the game to start earning trophies*. The page reads nothing until it is opened, re-reads only when those files change, and never writes to them.

**Input** (launcher: Controls › Aiming and sticks)

- Key / mouse-button rebinding: Controls page (`controls.txt`).
- `PAD_MOUSE_SENS_Y=<0.1..4>` — vertical mouse sensitivity relative to horizontal (direct and classic mouse aiming).
- `R2_ADS_SENS=<0.1..3>` — mouse sensitivity while aiming down sights / scoped, relative to hip fire (on top of `R2_ZOOM_SENS`).
- `PAD_AIM_TOGGLE=1` — Aim (L1 / Mouse2 / LB) toggles instead of hold, for keyboard and controller; menus and the pause screen release it.
- `PAD_DEADZONE=<0..0.9>` — controller stick deadzone, radial (unset: the XInput standard 24 % per axis, as before).
- `PAD_CURVE=<0.5..3>` — stick response exponent past the deadzone (1 linear, >1 finer near the centre, <1 faster).
- Invert vertical look: Controls page (`controls.txt`). DualShock 4 / DualSense / Switch: supported natively through SDL3 (see *Controllers (SDL3)* under Controls); Steam Input and DS4Windows are not needed.

**Mods folder** — `R2_MODS=1` (launcher: Mods folder = On) loads files from `mods\` next to the game (`R2_MODS=<folder>` names another folder). Each sub-folder is one mod laid out like the game's `PS3_GAME` folder: `mods\MyMod\USRDIR\<path of the file to replace>`. Mods are applied in alphabetical order (a later one wins on the same file); a folder name starting with `_` or `-` is skipped. Every file layer of the runtime (cellFs, sys_fs) uses the replacement. `logs\run.log` lists each mod and override at start-up (`[mods] override: ...`) and the first use of each (`[mods] using ...`). Single members of a `.psarc` archive and single level assets (by tuid) can be replaced as well: see [docs/MODDING.md](docs/MODDING.md). Single textures by content hash go through the renderer's texture packs (*Graphics enhancements*, `RSX_TEXPACK`).

**Shaders** — the Compatible renderer keeps every GPU pipeline it builds in `shadercache\rpcs3\` and, at start-up, builds all of them again before the first frame (RPCS3's shader cache preload), so after the first play-through nothing is compiled in game. The log shows the progress (`[r2rsx] shader preload: compiling n/N pipelines`, then the total time); `R2_SHADER_PRELOAD=0` keeps the preload but without the log lines. `R2_VK_PIPE_CACHE` (default on) also keeps the graphics driver's own pipeline cache on disk (`shaders_cache/vk_pipeline_cache.bin`, used only by the same GPU and driver build), so the start-up preload mostly reloads instead of recompiling; `0` starts with an empty driver cache every run as RPCS3 does. Every pipeline first linked during play is logged (`[r2rsx] shader cache miss #n during play`) and is preloaded from the next start. With `R2_RSX_PROF=1`, the `r8 per frame` line's *in-memory misses* and *draws skipped (program not ready)* should stay at 0 in a level you have played before. `R2_SHADER_CACHE=0` turns the disk cache off; `R2_SHADER_MODE` (launcher: Shader compilation) picks asynchronous / interpreter / synchronous compilation for pipelines that are not in the cache yet. The pipelines depend on the game's state and cannot be generated before they are first seen, but they can be recorded and shipped:

- `R2_FOSSILIZE=record` writes every pipeline built in a session (with its shaders, layouts and render pass) to `shadercache\rpcs3\shaders_cache\r2_pipelines.foz` (Valve's Fossilize format, the one Steam's shader pre-caching uses; sessions append). By default (`auto`) the renderer replays that file, if it is there, on low-priority threads after the start-up preload, so a new GPU or driver compiles them into its pipeline cache in the background. `0` = off; `R2_FOSSILIZE_DB=<path>` = another file.
- `R2_PRELOAD_ASYNC` (default on with the asynchronous shader mode, off otherwise) compiles every cached shader before the first frame but links only the first `R2_PRELOAD_BOOT` (384) pipelines recorded; `R2_PRELOAD_THREADS` (default a quarter of the CPU threads, 1 to 4) background threads link the rest while you play (`[r2rsx] shader preload: background done ...`). `0` = everything before the first frame, as before.
- `R2_VK_GPL` (default on when the GPU supports `VK_EXT_graphics_pipeline_library` with fast linking, logged as `[r2rsx] pipeline libraries (R2_VK_GPL): on`): a pipeline first needed during play is linked in well under a millisecond from separately cached parts, and the fully optimised pipeline is compiled in the background and swapped in. Same shaders and state; `0` = the normal path.
- `R2_PIPE_LOG` (default on) prints a `[pipe]` line every 5 seconds with the time the render thread spent waiting for shader or pipeline compilation, the number of waits over 2 ms (histogram) and the fast-link / background counts.

**Not done:** native HDR rendering (the game produces an 8-bit SDR image; the opt-in HDR output below maps that image into an HDR swapchain at a chosen paper white - brighter UI and correct SDR-in-HDR, but no extra highlight range; real HDR needs the game's tone mapping rewritten). Frame generation is available (see *PC extras*). A centred 16:9 HUD at ultrawide is available as an experiment (`RSX_HUD_FIX=1`, *Graphics enhancements*).

### PC extras

All of these are in the launcher (on the Display, Graphics, Upscaling & Frame Gen, Controls, Interface & Notifications and Advanced pages, see *All launcher options*, and the **Saves & tools** page) and most also in game: **F9**, or **F10 › More PC Options**. The in-game page writes the same `settings.ini` / `settings.bat` lines as the launcher. Rows marked *(next start)* apply after a restart; the rest apply at once. Code: `project\r2_pcextra.cpp` (game side), `project\launcher_extra.inc` (launcher), `ps3recomp\third_party\rpcs3\rpcs3\Emu\RSX\VK\VKR2PcExtras.cpp` (Vulkan HDR / colour-blind / Reflex).

- **Skip intro logos** — `R2_SKIP_INTRO` (default on; `0` = watch them). Skips the logo movie. The legal/health notice is a game screen that waits for input and is not skipped automatically (the code that sequences it was not located).
- **Key rebinding** — keyboard and mouse: launcher Controls page or in game *More PC Options › Key bindings* (Enter = press the new key, Esc = cancel; written to `controls.txt` and re-read at once). Controller: launcher *Controller buttons* or in game *More PC Options › Controller layout* — each Xbox button can act as another one (`PAD_MAP_<button>=<button>`, e.g. `PAD_MAP_A=B`), `PAD_SOUTHPAW=1` swaps the sticks. The remap is applied to the XInput state before the game sees it, so it also changes the game's prompts' meaning (the prompt still shows the PS3 button the game expects).
- **Automatic prompts** — `R2_PAD_GLYPHS=auto` (default): the prompts follow the last device used (keyboard/mouse → key names, controller → Xbox or PlayStation symbols by the controller's type). Already in the port; listed here for completeness.
- **Post effects** — `R2_FX_SCREEN_BLUR=0` (damage / sprint / explosion full-screen blur), `R2_FX_CAMERA_SHAKE=0`, `R2_FX_LETTERBOX=0` (cutscene black bars). Implemented by pointing the game's Lua script bindings for these effects at a no-op, so they switch live. Resistance 2 has no film grain, depth-of-field, chromatic aberration or vignette pass of its own to turn off (none were found among its render passes or script functions).
- **Colour-blind filter** — `R2_COLORBLIND=1|2|3` (protanopia / deuteranopia / tritanopia): daltonization in the final output pass (Compatible renderer). Applies at once.
- **Extra crosshair** — `R2_CROSSHAIR=dot|cross`: a small marker at the window centre during play (shown while the game's look input is active; hidden in menus, the PC menu and photo mode).
- **Window** — `RSX_RESIZABLE` (default on): the windowed-mode window has a sizing border and the swapchain follows the new size. `RSX_ALT_ENTER` (default on): Alt+Enter switches windowed ↔ borderless fullscreen.
- **Frame rate** — the cap stays the game's own frame limiter (`R2_FPS`, launcher *Frame rate*); there is only one limiter. 120 / 144 / Uncapped are marked *experimental*: the game measures real time per frame and the port rescales its 30 Hz frame-counted timers (`r2_timing_frames`, `R2_PATCH_TIMING_FRAMECOUNT`), so the game does not speed up, but its physics step is fixed at 60 Hz, so above 60 fps moving objects judder slightly and some camera smoothing was written for 30/60 fps and was not audited frame by frame.
- **NVIDIA Reflex** — `R2_REFLEX=1` (`2` = + Boost), opt-in, Compatible renderer, GeForce with `VK_NV_low_latency2`: low-latency mode plus the Reflex sleep at the start of each frame. It does not cap the frame rate (the game's limiter still does); no latency markers are sent, so the NVIDIA overlay's latency readout stays empty. Does nothing when the extension is missing.
- **DLSS Frame Generation** — `R2_DLSS_FG=2|3|4` (launcher: Upscaling & Frame Gen › *Frame Generation*; `0`/unset = Off), Compatible renderer, GeForce RTX 40 (2x) or RTX 50 (up to 4x Multi Frame Generation), Anti-aliasing = NVIDIA DLSS, setup's optional NVIDIA Streamline download. The GPU inserts 1-3 generated frames between the game's own (`ps3recomp\third_party\rpcs3\rpcs3\Emu\RSX\VK\VKR2FrameGen.cpp`, NVIDIA Streamline 2.14.16, manual hooking). Smoother motion when the CPU is the limit; adds a little input lag, so Reflex is switched on with it (Streamline drives Reflex while FG is set up; `R2_REFLEX=2` = Boost). The frame-rate setting caps the game's own frames; the Reflex cap keeps rendered × multiplier under the refresh rate. The HUD is split from the scene in the renderer (HUD draws after the 3D pass go to a UI layer that is composited after generation), so HUD text does not warp; when a frame's HUD can't be separated cleanly it is passed through untouched. FG pauses in menus, movies, loading screens, split-screen, while the game is paused and on scRGB HDR, and turns itself off (one `[fg-vk] frame generation off: ...` log line) on other GPUs, without the DLLs, or with FSR / XeSS / no AA. Debug: `R2_DLSS_FG_HUD=0` (no HUD split), `R2_DLSS_FG_HUD_DEBUG=1` (show the HUD-less image), `R2_DLSS_FG_MENU_OFF=0`, `R2_DLSS_FG_CAP=0`, `R2_DLSS_FG_YFLIP=0`, `R2_DLSS_FG_DEBUG=1`. With a fixed-refresh display and no G-Sync, vsync is replaced by the Reflex cap and may tear.
- **FSR 3 Frame Generation (fallback)** — `R2_DLSS_FG=fsr3` (launcher: *Frame Generation* › *FSR 3 2x*, listed on GPUs that are not RTX 40/50), or `R2_FG_BACKEND=auto|dlss|fsr3` (auto = default: DLSS-G first, then FSR 3 2x when DLSS-G is unavailable; `dlss` = DLSS-G only; `fsr3` = FSR 3 only, also on RTX cards). AMD FidelityFX frame interpolation (FidelityFX API 1.1.4, `tools\ffx_vk\amd_fidelityfx_vk.dll`, MIT, shipped with the port) on any Vulkan GPU with a spare present-capable queue and timeline semaphores; 2x only. Needs Anti-aliasing = FSR, DLSS or XeSS (for depth and motion vectors) and SDR or HDR10 output. Uses the same inputs, HUD split (the HUD-less scene goes to interpolation, the HUD layer is composited on every shown frame), camera-cut resets and pause rules as DLSS-G. No Reflex while it is selected (FidelityFX's swapchain has no low-latency mode), so it adds a little more input lag than DLSS-G; the game's frame cap still applies to its own frames and FSR 3 paces the generated ones. Turning it on or off mid-game waits for the GPU once (a short hitch). HDR10 works on the usual A2B10G10R10 swapchain; a driver that only offers A2R10G10B10 for HDR10, and scRGB HDR, keep FG paused. Debug: `R2_DLSS_FG_DEBUG=2` (FidelityFX tear lines and reset markers); the HUD switches above apply too. Off reasons are logged as `[fg-vk] ...` lines.
- **HDR output** — `R2_HDR=1` (HDR10 / PQ, `VK_COLOR_SPACE_HDR10_ST2084_EXT`) or `R2_HDR=scrgb` (FP16 extended linear sRGB), opt-in, Compatible renderer, Windows HDR must be on. The final output pass converts the SDR image to linear and scales it so SDR white = `R2_HDR_PAPER` nits (default 200), then encodes PQ or scRGB; `R2_HDR_PEAK` (default 1000) is sent as `VK_EXT_hdr_metadata` and caps highlights. Falls back to SDR when the display/driver has no HDR surface format (the log says so). The F3 overlay, PC menu and screenshots (GDI) stay SDR.
- **Hold to skip movies** — `R2_CUTSCENE_SKIP` (default on): hold Space / Enter / A / Menu for 1 s while a pre-rendered movie plays and the port presses the game's own skip button (the game lets you skip its movies). In-engine cutscenes are not skipped (not safe: they drive level scripting). The *Hold Space / A to skip* notification shown when a movie starts can be hidden with launcher *Show cutscene skip hint: Off* (`PORT_SKIP_HINT=0`, or `R2_SKIP_HINT=0`); holding still skips.
- **Subtitles** — not available: the subtitles are drawn by the game's Scaleform movies at a fixed size; there is no size or background setting to hook.
- **Discord Rich Presence** — `R2_DISCORD=1`, opt-in. Talks to the local Discord app over `\\.\pipe\discord-ipc-0..9` (no SDK): *Resistance 2* plus the current level (from the level files the game opens). Needs an application id: create one at discord.com/developers and put the number in `discord_appid.txt` in the game folder or `R2_DISCORD_APPID`. Silent when Discord isn't running (retries every minute).
- **Photo mode** — `R2_PHOTO` (default on; `0` = off): **F7** pauses the game, hides the port's overlays (F3, crosshair, hints) and shows a short note; **F12** takes the screenshot; F7 again resumes. There is no free camera (the game's camera is driven by its own update code that is not hooked safely yet) and the game's HUD cannot be hidden separately (it is drawn into the same target as the 3D scene).
- **Pause when the controller disconnects** — `R2_PAD_DISCONNECT_PAUSE` (default on): when the controller in use disappears the game pauses with a message until it is reconnected or Enter is pressed.
- **Benchmark** — launcher *Saves & tools › Run benchmark* (or `R2_BENCH=1`): the game follows a fixed input script from start-up into the first level (`R2_BENCH_SCRIPT`, the `scratch\heliboot.ps1` route), measures `R2_BENCH_SECONDS` (default 60) and quits (`R2_BENCH_QUIT=0` keeps it running). Writes `logs\bench\R2_bench_<date>.txt` (average, 1 % and 0.1 % low fps, median / p99 / max frame time, draws per frame, µs per draw) and appends `logs\bench\bench_history.csv`; the launcher shows the last result. The launcher's button also sets `R2_BACKGROUND=run` and turns the pad-loss pause off for the run.
- **Save manager** — launcher **Saves & tools**: lists the saves (title from `PARAM.SFO`, date, size), *Back up now*, per-backup *Restore* (the current saves are backed up first as `_before-restore`) and *Delete*, and *Open* buttons. *Back up saves before playing* (`R2_SAVE_BACKUP`, default on, keeps the newest 10 automatic backups; `5` / `30`; `0` = off): Play copies the save folder to `save_backups\<date>_auto` when it changed since the newest backup.

**How to check them in game:** F9 opens *More PC Options*; toggle Screen blur and take damage / sprint; Colour-blind filter changes colours at once; F7 = photo mode; unplug the controller during play = pause message; hold Space during the intro movie of a level = skip; drag the window edge in windowed mode; Alt+Enter; `logs\run.log` lines start with `[r2x]` (options, level names, Discord, benchmark result).

## Graphics options

- **Frame rate:** 60 fps by default (the game was 30 fps on PS3). The game's own frame limiter is replaced by a precise PC-side one, the same idea as RPCS3's "Unlock FPS" patch. 30 fps is still available in the launcher.
- **Display mode:** borderless fullscreen, exclusive fullscreen or windowed, with VSync off / on / adaptive.
- **Anti-aliasing → NVIDIA DLSS** uses DLSS on GeForce RTX cards and falls back to FSR 3.1 on other GPUs. The DLSS SDK is downloaded by Setup into `tools\dlss` (if that fails, the port still builds without it).
- **Upscaling** (with FSR or DLSS on): Native (DLAA), Quality, Balanced or Performance. The default is DLSS Performance at a 4K render resolution, which is then downscaled to your screen: sharper than native at about the cost of native. Menus and HUD stay at full resolution.
- **Intel XeSS (experimental, Compatible renderer):** XeSS 2 (`rpcs3/Emu/RSX/VK/VKR2XeSS.cpp`, Vulkan `xessVK*` API, Intel's `libxess.dll`; runs on any GPU through DP4a) is hooked into the RPCS3 renderer's upscaler. Pick **Intel XeSS** in the launcher's upscaling option, or set `RSX_FSR=4`. If XeSS can't start, or on the native renderer, FSR 3.1 is used instead. The preset follows the Upscaling ratio (Native → Native AA, Quality → Ultra Quality, Balanced → Quality / Balanced, Performance → Balanced / Performance). `RSX_XESS_QUALITY=aa|uqp|uq|q|b|p|up` asks for a preset, `RSX_XESS_JITTER_SIGN=-1` / `RSX_XESS_JITTER_SIGN_Y=-1` flip the jitter, and `RSX_XESS_LOG=1` logs XeSS's own messages. Setup downloads the XeSS SDK (`tools/xess/`) after you accept Intel's licence.
- Moving objects (like rotating weapon pickups) are masked for DLSS so they don't leave ghost copies.
- **Low-end PCs:** the launcher's **Recommended for this PC** button picks settings for your CPU and GPU. Tuned for a 4-core CPU, a GTX 1050-class GPU and 8 GB of RAM; on 4 cores the first level holds 60 fps. On CPUs with fewer than 8 threads the port stops raising thread priorities and pinning threads to cores, and its busy-wait loops give the CPU to other threads. If a 4-core PC still stutters, use 30 fps. Advanced: `PS3_PRIO_BOOST=0/1` forces priority boosts off or on; `PORT_MAIN_CPU=<n>` pins the main thread to CPU n.
- **Shader-program re-sends** (renderer, on by default): the game sends the same shader programs again hundreds of times per frame. A re-sent fragment program with the same code only has its constants refreshed (`R2_FP_REBIND=0` turns this off), a vertex program uploaded or selected again with the same code keeps its analysis (`R2_VP_REBIND=0`), and when only render state changed and the previous draw's pipeline still matches it is reused without a cache lookup (`R2_PIPE_MEMO=0`). `R2_RSX_PROF=1` shows them as *re-sent FP/VP kept* and *pipeline memo hits*. All renderer switches are listed in `ps3recomp/third_party/rpcs3/README.port.md`.
- **Skipped re-uploads** (renderer round 8, on by default, output byte-identical): transform constants that have not changed since the last upload for the same vertex program are not uploaded again (`R2_XFORM_MEMO=0` turns this off), and an index range identical to one already converted this frame reuses the converted copy (`R2_IDX_DEDUP=0`). `R2_FIFO_MERGE=1` (off by default) lets RPCS3 merge back-to-back draws from 400 draws per frame instead of 2,000 (`R2_FIFO_MERGE=<n>` sets the threshold). Asynchronous texture streaming (`R2_ASYNC_TEX=1`) stays off: it only moves the GPU copy to another queue (the CPU work stays on the render thread), stops texture memory reuse and adds queue hand-overs, and the profiler shows almost no texture uploads in play (`texture uploads` on the `r8 per frame` line). `R2_RSX_PROF=1` prints two `r8` lines (where the end of each draw goes, Vulkan recording share, skipped bytes, pipeline misses, FIFO redundancy); adding `R2_RSX_CENSUS=1` prints `r9 census` lines (most frequent GPU commands, render-target, depth and texture formats) used to pick the next game-specific fast paths.
- **ZCULL labels over a flip** (`R2_ZCULL_FLIP_LAG=<n>`, experimental, **default 0 = off**): in strict ZCULL mode, lets occlusion labels still waiting on the GPU be carried over up to `n` flips instead of waiting at every flip. They are also released whenever the RSX command queue runs dry. It hung Resistance 3 in one bench, so leave it off unless you are testing it.
- **CPU profile** (Performance page): Auto picks from the cores the game can use (logged as `[cpu]`). Low (≤4 cores) turns off the timing spin-waits, folds the Vulkan submit thread into the render thread and uses fewer shader-compiler threads. Mid (6 cores) shortens the spin-waits. High changes nothing. The port also spreads its main, RSX and FIFO threads over separate cores, keeps them on P-cores on hybrid Intel CPUs and lowers background threads. Advanced: `PORT_CPU_PROFILE=auto|low|mid|high|off` (env settings you set yourself always win); `PORT_THREAD_PLACEMENT=0` turns off thread placement. See *CPU performance switches* below.

## Shader precompile and rebuilding the shader cache

Shader caches are never shipped with the port: they are built on your PC, from the shaders your game uses, the first time each one is seen. Launcher, Performance page, **SHADER CACHE**:

- **Compile shaders before playing** (default On, `R2_PRELOAD_BOOT=-1`): at start-up the Compatible renderer builds every pipeline in its disk cache before the game gets its first frame (the game's RSX context waits for it), so nothing seen before compiles during play. Progress: a centred **Compiling shaders N / M** panel with a progress bar, drawn into the game image (`R2_PRELOAD_OSD=0` uses a separate window, not shown over exclusive fullscreen) and log lines `[r2rsx] shader preload: compiling every cached pipeline before the game starts on N threads`, `compiling n/N pipelines`, then `done, every cached pipeline is ready`. A few seconds with a warm driver cache, a minute or two after a driver update or a rebuild. Off (`R2_PRELOAD_BOOT` unset, 384 by default): only the first 384 recorded pipelines are built before the first frame and the rest in the background while you play (can hitch briefly).
- **Shader compile threads** (`R2_PRELOAD_BOOT_THREADS`, default Auto = every logical core but one, 1 to 64): the threads that build the pipelines before the game starts.
- **Shader interpreter while compiling** (`R2_SHADER_INTERP=1`, default Off): RPCS3's "Async with Shader Interpreter": a shader that isn't compiled yet is drawn by the interpreter instead of being skipped (no briefly invisible geometry on a cache miss; slower frames while it is active).
- **Rebuild shader cache** (button, asks first; refused while `Resistance2.exe` is running) deletes the compiled, driver-specific part and keeps the list of shaders and pipelines to build, so the next start with Compile shaders before playing on rebuilds all of them. Deleted, from `shadercache\` and from its backup in `%LOCALAPPDATA%\Resistance2Port\shadercache` (`run.bat` restores missing files from there):
  - `rpcs3\shaders_cache\vk_pipeline_cache.bin` (and `.bin.tmp`): the Vulkan driver's pipeline cache (driver blobs)
  - `rpcs3\shaders_cache\r2_gpu_stamp.txt`: the GPU and driver that cache was built for
  - `rpcs3\shaders_cache\shaderscan.stamp`: makes the launcher run shaderscan again on the next Play (see below)
  - `rpcs3\shaders_cache\r2_pipelines.foz`, `r3_pipelines.foz`: Fossilize databases (`R2_FOSSILIZE`)
  - `vk_pipeline_cache.bin`: the native renderer's Vulkan pipeline cache

  Kept: `rpcs3\shaders_cache\pipelines\vulkan\...\*.bin` and `raw\*.vp` / `*.fp` (RPCS3's pipeline list and shader programs: what gets rebuilt) and the native renderer's `vk_pipeline_variants.bin`, `rsx_engine_programs.bin` and `pipelines_*.db` lists.
- **GPU or driver change:** `r2_gpu_stamp.txt` records vendor, device, driver version, pipeline cache UUID and GPU name when the driver cache is saved. When they differ at start-up the log says `[r2rsx] vk pipeline cache: GPU or driver changed since the cache was built (was: ..., now: ...)`, the old `vk_pipeline_cache.bin` is deleted and every pipeline is rebuilt from the kept list (before play with the option on).
- **Offline shader scan:** [shaderscan](https://github.com/miroslavdanutalexandru-alt/shaderscan) (GPL-2.0, its own repository) is downloaded by Setup / `build.bat` into `tools\shaderscan.exe`: `tools\get_shaderscan.ps1` fetches the release pinned in `tools\shaderscan.version` and checks its SHA-256 (offline: skipped; set `SHADERSCAN_EXE=<path>` to use your own build). It is not part of the port's source or Setup, and the launcher works without it. When `shaderscan.exe` is next to `Launcher.exe` (or in `tools\`), Play first runs `shaderscan --check --out shadercache\rpcs3\shaders_cache`; if that says the cache isn't seeded (first launch, a new shaderscan version, or after Rebuild shader cache), it runs `shaderscan --game <folder> --out ... --progress`, which extracts every RSX shader from your game files into the cache so Compile shaders before playing covers the whole game without a playthrough. The launcher shows a progress panel (Esc or **Skip for now** stops it; the game starts either way) and logs the tool's output to `logs\shaderscan.log`. It runs at most once per launcher session.
- **Object motion vectors** (`RSX_DLSS_OBJMV`, default Off, Compatible renderer): DLSS/FSR get per-object motion for moving characters and objects (sharper, less ghosting on them) at some CPU cost; Off = camera motion only.

## CPU performance switches and builds

The frame rate in heavy scenes is set by two threads: the game's main PPU thread and **rawSPU0**, the SPU job kernel (the main thread waits on its jobs). Env switches (unset = default):

- `PORT_SPU0_CPU=<n>` — with ≥ 12 logical CPUs rawSPU0 gets logical CPU 6 and an empty SMT sibling, like the main thread (`PORT_MAIN_CPU`, default 2) and the RSX thread (`PORT_FIFO_CPU`, default 4). `-1` turns this off. In the Compatible (RPCS3) renderer the RSX core is the thread named `rsx::thread`, which now gets the `PORT_FIFO_CPU` core (it was left with the other threads before and that core sat idle). Everything else shares the remaining cores. The log prints `[affinity] ... pinned` lines. `PORT_MAIN_CORE=0` turns off all of this pinning.
- `PORT_PCORE_HOT=0` — on Intel hybrid CPUs (P + E cores), main, rawSPU0, `rsx::thread` and the FIFO / RSX engine threads get thread CPU sets for the P-cores (`SetThreadSelectedCpuSets`), also with fewer than 4 P-cores. Hard pins only use P-cores. AMD and other uniform CPUs are not changed.
- `PORT_KEEPER_MS=<ms>` — the two thread-placement keepers rescan the process threads every 1–2 s while new threads appear, then every 5000 ms (each scan walks every thread on the system; at 1 Hz they cost 4–7 % CPU each).
- `PORT_PIN_TOPO=0` — the default pin CPUs above (main, RSX, rawSPU0) are now taken from the detected CPU topology: the first logical CPU of physical cores 1, 2 and 3 (P-cores first on hybrid CPUs). On a 6-core/12-thread CPU that is still 2, 4 and 6; on CPUs without SMT it is 1, 2 and 3 instead of two hot threads sharing a core. `=0` uses the old fixed 2 / 4 / 6. The `[affinity] main thread ...` line shows the CPUs and the switch values.
- `PORT_REST_SMT=<0|1|2>` — where all other threads (SPURS lanes, rawSPU1, RSX workers, driver, audio) may run. `0` (default): only cores that have no pinned hot thread, so every hot thread keeps its SMT sibling idle (on 6C/12T that is mask `0xF03`). `1`: also the siblings of the rawSPU0 and RSX cores. `2`: also the main thread's sibling. Try `1` if the `[thrcpu]` lines show the SPURS lanes starved; it costs the hot threads some SMT throughput.
- `PS3_USLEEP_NOTIFY_PUT=0` — a short guest `usleep` poll (under 1 ms) already ends early when an SPU atomic writes guest memory (`PS3_USLEEP_NOTIFY`). It now also ends early on a plain SPU DMA PUT, so the main thread's SPU-job wait (`guestwait` at `0x006B8D3C`) re-checks as soon as a job writes its result instead of finishing its 128 µs sleep. It costs nothing while no thread is waiting. `=0` turns this off. Logged once as `[usleep] PS3_USLEEP_NOTIFY=1 PS3_USLEEP_NOTIFY_PUT=1 ...`.
- `SPU_INLINE_CH=0` — the lifted SPU code writes the MFC DMA parameter channels (LSA, EAH, EAL, size, tag, tag mask) and reads the status channels directly instead of calling the channel runtime (thousands of call sites, each of which also reloaded every live register). The command and blocking channels still go through the runtime. `=0` restores the calls. Build option `-DPORT_SPU_INLINE_CH=OFF`.
- `PORT_SPU_SPINWAIT=old|backoff|wait` — how a raw SPU spins in a blocking channel wait (mailbox, signal, event / lost-reservation) before it sleeps on the wake-up. `old` (default): the original tight spin. `backoff` (experimental, no measured gain): `_mm_pause` bursts that grow 4 → 128 per poll, then `SwitchToThread` on each poll after 20 µs, so the core is shared with the main thread and the other SPUs instead of burned. `wait`: the same but goes to sleep after `PORT_SPU_SPINWAIT_US` (default 20) instead of the full `SPU_WAIT_SPIN_US`. Producers already wake the waiter. Logged every 10 s as `[spuwait] mode=... waits= done-in-spin= slept= polls= yields= wakes=`.
- `R2_OCC_SKIP=0` (default) — occlusion-query results. Each frame the game waits for the GPU to finish last frame's occlusion queries before it continues, so the CPU and the renderer take turns instead of working in parallel. With `=1`, when a result is not ready yet, the game keeps the previous frame's visibility for that set once, and waits normally on the next frame. Measured in a heavy fight: 35.7 → 46.1 fps, but nearby objects visibly pop in and out, so it is off by default. `=0` is the original wait. `R2_OCC_WAIT_US=<n>` caps the wait at n µs (`0` = never wait, which leaves objects stuck hidden, so don't use it). Logged every 10 s as `[occ] report sets ... ready / skipped / waited`.
- `R2_FIFO_INLINE=1`, `R2_DMA_INLINE_MAX=<bytes>`, `R2_OM_FASTHASH=1` — renderer-thread changes: an inline fast path for reading FIFO command arguments; RSX DMA copies up to this size run directly on the RSX thread instead of waking the Offloader thread (default `0` = the upstream 3.5 KB limit; `65536` was the earlier default); and a cheaper hash for the DLSS object-motion vertex key. Together they saved about 1 % per draw. All three are off by default since a 2026-11-11 freeze report (RSX FIFO stalled with get ≠ put); they were not confirmed as the cause, but they are only worth about 1 %. The rest of the RSX thread's time is spread over many functions; the profile is in `scratch\r2_hotpath.md` (section 7). The largest single cost left is DLSS object motion vectors (`RSX_DLSS_OBJMV=0`: 43 → 47 fps in the airfield fight, at some DLSS quality cost).
- `R2_HOT_NATIVE=1` (experimental, off by default; no measured gain) — the game's own memcpy, memset, strcmp, strlen, strcpy, memcmp and its signed string compare (`0x006BB908`) run as native host code when the call is in the plain case (no overlap, ordinary guest memory, no SPU-reserved destination line); otherwise the translated code runs. With it on, `R2_HOT_MEMCPY=0`, `R2_HOT_MEMSET=0`, `R2_HOT_STRCMP=0`, `R2_HOT_STRLEN=0`, `R2_HOT_STRCPY=0`, `R2_HOT_MEMCMP=0`, `R2_HOT_GSTRCMP=0` one at a time. One call in `R2_HOT_SAMPLE` (default 4096) runs both versions and compares them; every 10 s `[hot]` lines give calls, the native share, cycles per call translated vs native, the estimated time saved and any mismatch (`R2_HOT_LOG=0` hides them). Details: `scratch\r2_hotpath.md`.
- `SPU_PROF=1` (also on with `R2_RSX_PROF=1`) — every 5 s, a `[spu-prof]` line per raw SPU and for the SPURS/thread SPUs: % of time in channel waits (spin / sleep split), GETLLAR (and lock-free share), PUTLLC (and fail %), DMA commands and MB/s, channel runtime calls and indirect branches per second.

Not done, and why: large pages for guest memory (the 4 GB guest reservation is committed lazily on fault and the RPCS3 texture cache write-protects single 4 KB guest pages, neither of which works on 2 MB large pages; large pages also need the *Lock pages in memory* right). Running SPU jobs in parallel (the job kernel runs a job list in order and jobs share data; nothing in the image says which are independent). The main thread's SPU-wait spin (`r2_spin_relax`) is unchanged: main has its own core, so it only shows how long rawSPU0 takes.

**Builds** (`build.bat` is unchanged; each of these goes into its own folder, and `run.bat` runs `PORT_GAME_EXE` when it is set, e.g. `set PORT_GAME_EXE=build_cpu\Resistance2.exe`):

- `build_cpu.bat` → `build_cpu\Resistance2.exe`: AVX2 for the runtime and hand-written code too (the lifted code already uses it) plus ThinLTO (clang-cl `-flto=thin`, lld-link) for the runtime, hand-written and lifted SPU code. The lifted PPU objects are reused from `build\` (they are 1.3 GB of C++). This toolchain is clang-cl, not MSVC: `/GL` and `/LTCG` do not apply, and `-flto=thin` is the equivalent. `PORT_LTO=0` skips LTO. `PORT_LTO_PPU=1` also includes the lifted PPU code, which takes hours to link.
- `build_cpu.bat baseline` → `build_baseline\Resistance2.exe`: no AVX anywhere, for CPUs older than Haswell / Zen (compiles the lifted PPU code again). When it exists, the launcher's CPU check makes `run.bat` use it on CPUs without AVX2/FMA/BMI2 (`PORT_CPU_V3=0`).
- **PGO** (clang instrumentation PGO, same scope as LTO, `PORT_PGO_PPU=1` adds the lifted PPU code):
  1. `build_cpu.bat pgo-gen` → `build_pgo_gen\Resistance2.exe` (instrumented).
  2. `tools\pgo_train.bat`: play 10–20 minutes covering heavy fights, vehicle sections, a level load, a checkpoint reload, cutscenes and menus. The profile is written every 30 s (`PORT_PGO_DUMP_S`) and at exit to `pgo\raw\`, and repeated sessions add up.
  3. `tools\pgo_merge.bat` → `pgo\r2.profdata`.
  4. `build_cpu.bat pgo-use` → `build_pgo\Resistance2.exe`.

  Record again after larger code changes.
- CMake options behind these: `PORT_ARCH=AVX2|baseline`, `PORT_LTO`, `PORT_LTO_PPU`, `PORT_PGO=off|gen|use`, `PORT_PGO_PROFILE`, `PORT_PGO_PPU`, `PORT_PPU_PREBUILT_OBJ_DIR`, `PORT_SPU_INLINE_CH`, `PORT_TRACY`, `PORT_TRACY_DIR`.

**Tracy timeline** (optional; normal builds are unchanged): `build_cpu.bat tracy` → `build_tracy\Resistance2.exe`, built with the [Tracy](https://github.com/wolfpld/tracy) client from `PORT_TRACY_DIR` (default `D:\Games\refs\tracy`, v0.15). Run it with `set PORT_GAME_EXE=build_tracy\Resistance2.exe` and `run.bat`, then connect the Tracy 0.15 viewer (`tracy-profiler.exe`, from the Tracy releases or built from `profiler\`). The client is on demand: nothing is recorded until the viewer connects. Zones per thread:

- `syscall` (value = lv2 syscall number) and `hle` (value = import NID): every guest syscall and HLE import call, so a PPU thread's sleeps, lwmutex/cond waits and SPURS/GCM waits show up.
- `spu job` (value = overlay id << 32 | entry pc): one WWS job on an SPU thread (rawSPU0 is the job kernel), and `spu wait`: one blocking channel wait (value = channel).
- `spin`: the main thread's SPU-job spin-wait (`r2_spin_relax`), one zone per wait, merged from its laps (laps closer than `PORT_TRACY_SPIN_GAP_US`, default 20 µs; shorter than `PORT_TRACY_SPIN_MIN_US`, default 5 µs, are dropped).
- Frame marks `RSX flip`, when `rsx::thread` reaches a guest flip (polled every 0.25 ms, `PORT_TRACY_FRAMES=0` turns them off). The RSX FIFO loop and present are inside the prebuilt RPCS3 library, so they have no zones of their own. Run as administrator for Tracy's context-switch view and call-stack sampling of `rsx::thread`, the audio threads and the lifted code.

Tracy's crash handler is off in this build, because the port's own exception handler commits guest memory on access faults.

## Audio

When the frame rate drops, the game's own audio mixing (on the recompiled PPU/SPU threads) can fall behind real time. The port therefore keeps a buffer of mixed audio between the game and the sound card, as RPCS3 does with its "audio buffering" and "time stretching" options:

- **Buffer:** mixed audio goes into a ring buffer with a target of `PORT_AUDIO_BUFFER_MS` (100 ms by default). The sound card is kept topped up from it, about 30 ms at a time.
- **Time stretching:** when the buffer drops below `PORT_AUDIO_STRETCH_THRESHOLD` % of the target, playback slows down through [SoundTouch](https://www.surina.net/soundtouch/) without changing the pitch, so a slow stretch of frames sounds slightly slower instead of gapping. Tempo returns to 1.0 as the buffer refills.
- **Underrun:** if the buffer still runs dry (a hitch longer than the buffer), the last few milliseconds fade out to silence and fade back in when audio returns. No repeated blocks, no clicks.
- **Mixer thread:** runs at TIME_CRITICAL priority with Windows MMCSS "Pro Audio" scheduling. It also stays off the cores reserved for the main game thread, the RSX thread and rawSPU0 (`PORT_MAIN_CPU`, `PORT_FIFO_CPU`, `PORT_SPU0_CPU`).
- **Late game audio:** when the game's next block of audio has not been written in time, the mixer waits up to `PORT_AUDIO_LATE_WAIT_MS` (22 ms) for it. The game's audio clock slips slightly and the buffer absorbs the slip.

| Variable | Default | Effect |
|---|---|---|
| `PORT_AUDIO_STRETCH` | `1` | SoundTouch buffer and time stretching. `0` = the previous audio output path, unchanged (the older built-in WSOLA stretcher; `PORT_AUDIO_WSOLA=0` turns that off too). |
| `PORT_AUDIO_BUFFER_MS` | `100` (`150` with fewer than 12 logical CPUs) | Target buffer, 20–500 ms. More = fewer underruns, more latency. |
| `PORT_AUDIO_STRETCH_THRESHOLD` | `75` | Stretch below this % of the target buffer. |
| `PORT_AUDIO_STRETCH_MIN` | `0.5` | Slowest tempo while stretching (0.1–1). |
| `PORT_AUDIO_FADE_MS` | `5` | Fade-out / fade-in length on an underrun. |
| `PORT_AUDIO_DEVICE_MS` | `30` | Audio handed to the sound card at once. |
| `PORT_AUDIO_CPU` | – | Pin the mixer thread to one logical CPU; `-1` leaves its affinity alone. |
| `PORT_AUDIO_MMCSS` | `1` | `0` = no MMCSS "Pro Audio" scheduling for the mixer thread. |
| `PORT_AUDIO_LATE_WAIT_MS` | `22` | Longest wait for a late block of game audio (0–40, `0` = off). |
| `PORT_AUDIO_LOG` | `5000` | Period of the `[audio]` log line in ms. `1` = every second (also with `R2_RSX_PROF=1`), `0` = off. |

**Reading the log.** At start-up, `logs\run.log` shows `[audio] output: ... SoundTouch stretch (...)` with the MMCSS state and the mixer thread's affinity. Every 5 seconds there is a line like:

```
[audio] 5.0 s: buffer min 82 avg 101 ms (target 100) | tempo min 1.00 avg 1.000 (stretched 0%) | underruns 0 (silence 0 ms, device dry 0) | ...
```

- `buffer min` well above 0 and `avg` near the target: healthy.
- `tempo min`/`avg` below 1.00 and `stretched` above 0 % during a frame-rate drop: the stretcher is covering it. Tempo should be back to 1.000 within a second or two.
- `underruns` and `device dry` should stay at 0. A non-zero count means a hitch longer than the buffer; try a larger `PORT_AUDIO_BUFFER_MS`.
- `ring overflow` at the end: the game produced audio faster than it was played, so the oldest audio was dropped (harmless after a long pause).

The offline test for this stage is `ps3recomp\libs\audio\tests\test_audio_out.cpp` (part of `scratch\enginetest\run_all.ps1`).
## RPCS3 patches

The community RPCS3 patches for Resistance 2 (RPCS3's official `patch.yml`, also mirrored in illusionyy/rpcs3-game-patch) are ported here. The PowerPC code is recompiled, so byte patches to instructions become hooks (`tools/hooks/r2.json`), and data patches are written into game memory at boot (`project/r2_patches.cpp`), only if the original value is there (EBOOT 1.00, PPU hash `08da3b601b335c5b09e6425b27fe00eacdad6e78`).

| Patch (author) | What it does | Guest address(es) | How it's ported | Setting | Default |
|---|---|---|---|---|---|
| Unlock FPS (Whatcookie): wait removal | Removes the frame limiter's wait | `0x00635D7C`, `0x00635D88` (nop) | Hook in `func_00635D28` (`r2_frame_period`, `project/stubs.cpp`): the game's own wait is bypassed and frames are paced on the PC side | `R2_FPS=30` / `<Hz>` / `refresh` / `uncapped` | 60 (launcher) |
| Unlock FPS (Whatcookie): frame-time floors | The game clock's minimum frame time: 1/30 s initial value, 1/75 s lower clamp → 0.0001 | `0x00D529D8`, `0x00D69EB8` (float) | Data patch at boot | Automatic when `R2_FPS` ≠ 30; `R2_PATCH_FPS_FLOORS=1` also applies it at 30 fps | follows `R2_FPS` |
| Extended FOV (illusion) | Wider field of view. RPCS3's note: breaks shadows (misaligned, jumping when the player moves). The port's default *safe* mode fixes that: see *Graphics enhancements* | `0x00DE67E4` (float 1.0 → 0.7) | Safe mode: the frustum-setter hooks (`r2_frustum_aspect`) widen only the 16:9 scene camera by the same amount (the first-person arms/weapon keep the original FOV, so their cut-off end stays off screen; `R2_FOV_VIEWMODEL=1` widens them too); `R2_FOV_SAFE=0`: data patch at boot, as RPCS3 | `R2_PATCH_FOV=1`; `R2_PATCH_FOV_VALUE=0.3..1.0` (lower = wider; setting it alone also turns the patch on; launcher / PC menu *Field of view*, live in the PC menu) | off |
| Port's own timing fix (not an RPCS3 patch) | Frame counts that assume 30 Hz are scaled by fps/30, so they keep their real-time length at 60+ fps: a 150-frame deadline, and once-a-second / every-2-seconds periodic checks | `func_0017F8EC`, `func_005140A8`, `func_0052BE60`, `func_0057CDEC`, `func_00599620` | Hooks (`r2_timing_frames`, `project/r2_patches.cpp`) | `R2_PATCH_TIMING_FRAMECOUNT=0` turns it off | on (no effect at 30 fps) |
| Port's own: native scene resolution | Turns off the game's dynamic resolution (the 3D scene drops from 1280x704 to 1152x640 on frames the game thinks are GPU-heavy). Same effect as the game's debug argument `-no_dynares` | `0x00DF7FDC` (word 1 → 0) | Data patch at boot | `R2_PATCH_FULLRES=1` | off |
| Port's own: no load shedding | The same governor also shortens LOD distances (x0.75) and trims effect budgets on GPU-heavy frames; this turns the whole GPU-time path off (implies `R2_PATCH_FULLRES`) | `0x00D52604` (float 2000.0 → 1e20, read only by `func_002C3780`) | Data patch at boot | `R2_PATCH_NO_LOADSHED=1` | off |
| Port's own: LOD distance | Multiplies the LOD transition/fade distances | `func_001721C8` (`0x00172450`) | Hook (`r2_lod_scale`, `project/r2_patches.cpp`) | `R2_PATCH_LOD_SCALE=1.0..4.0` | 1.0 (off) |

Not ported: the 1.60 and PSN (NPEA00431) versions of the same patches (different EBOOT), and the Artemis collection's "Infinite health" / "Endless ammo" for BCUS98120 1.60 (cheats, wrong version). No community patch exists for this game for motion blur, depth of field, film grain, shadow resolution, LOD or SPU load.

## Graphics enhancements

Beyond DLSS / FSR, all optional and off by default (launcher: Graphics page, "QUALITY" and "VIEW"). Each logs a `[patch]` line at boot.

- **Native scene resolution** (`R2_PATCH_FULLRES=1`). Resistance 2 has dynamic resolution: its render targets are 1280x704, and each frame a governor (`func_002C3780`) reads the previous frame's GPU time from RSX timestamps; when it is over budget it draws that frame's 3D scene at 1152x640 inside the same targets, and the final pass stretches it 10/9 onto the 1280x720 display. This patch keeps every frame at 1280x704 (the targets' full size, so nothing can be cut off; the remaining 704 → 720 stretch is 2%). With `RSX_RES_SCALE=3` the scene is 3840x2112 instead of 3456x1920; `RSX_RES_SCALE=1.5` gives a 1920x1056 scene for 1080p → DLSS 4K. Rendering a true 1280x720 scene was not done: the 704-line targets are allocated at boot in a fixed video-memory layout shared by several systems, and the gain over 704 is under 3%.
- **No load shedding** (`R2_PATCH_NO_LOADSHED=1`). Disables the governor's GPU-time path entirely: no resolution drop, no LOD-distance cut (x0.75) and no effect-budget cuts (x0.5 / x0.25 / x0.05) on slow frames. The CPU-time path is left as is.
- **LOD distance** (`R2_PATCH_LOD_SCALE=1.25 / 1.5 / 2 / 3 / 4`). Detailed models stay in use further away and distant objects fade out later. The multiplier is the one the game itself lowers to 0.75 under load; values above 1 are beyond what the game was tuned for, so watch for pop-in changes and frame time. The LOD rebuild (`func_00652F40` / `func_00652C24`) only clamps against small minimum distances, so values up to 4 are passed through unchanged. Far clip, foliage and particle limits were not found as separate tunables; on this game the LOD fade distance is what culls distant objects.
- **Field of view without broken shadows** (`R2_FOV_SAFE`, default on whenever a FOV is chosen). RPCS3's Extended FOV patch lowers the constant term of the game's `tan()` (`func_00788068`). Every `tan()` in the game shares that constant — the shadow-cascade fitting and the light frustums too — so the shadow maps stop covering what the camera sees and jump as it moves. Safe mode leaves the constant at 1.0 and widens only the 16:9 scene camera by exactly the same amount, in the two frustum-setter hooks (the culling frustum follows). The launcher / PC menu choice and the live change in the PC menu work as before. `R2_FOV_SAFE=0` restores the plain RPCS3 patch.
- **Shadow resolution** (`RSX_SHADOW_SCALE=2 / 4`, Compatible renderer). The game's shadow maps (1024² and 256² depth-only targets) are rendered at 2x / 4x per side by the renderer: only depth-only, square, power-of-two targets between 256 and 2048 are scaled (capped at 8192), they are sampled at their own scale, and the slope-scaled depth bias is multiplied by the same factor to avoid shadow acne. Costs GPU time and video memory (a 1024² map becomes 4096² at 4x).
- **Full-resolution effects** (`RSX_FX_FULLRES=1 / 2`, Compatible renderer). The PS3 draws particles, smoke, glow and the blur inputs (bloom, depth of field, motion blur) into half- and quarter-resolution targets. `1` renders the half-resolution passes (half the scene size) at full resolution, `2` also the quarter-resolution ones (bloom). The game's own upsample and composite passes then read them at their new resolution, so they need no change. This is how depth of field and motion blur get better here: the game's passes, at full resolution; there is no separate replacement pass.
- **Ultrawide HUD** (`RSX_HUD_FIX=1`, experimental, Compatible renderer, with `R2_ASPECT`). The 3D view stays Hor+ and stretched to the window; the HUD and menus (drawn after the 3D scene into the final picture) are squeezed back into a centred 16:9 area, so they are not stretched. Only 2D (orthographic, w = 1) vertices are moved, the same split RT64 uses (gameplay projection widened, 2D anchored to a 16:9 safe area): 3D draws, full-screen quads (the tone-mapped scene, fades, damage and scope overlays, letterbox bars) and edge-to-edge vertices stay where they are. The 3D view is widened game-side (scene camera frustums only, with K = 1 for every other `tan()`), so shadows stay aligned as with *FOV safe*. Limits: world-anchored markers (objective and grenade markers) drift towards the centre, and HUD elements meant for the screen edge sit at the edges of the 16:9 area. Because of that, `R2_ASPECT` stays off by default (also on non-16:9 displays): turn on *Ultrawide* and *Ultrawide HUD* together.
- **Ambient occlusion** (`RSX_SSAO=1`, experimental, Compatible renderer). A screen-space AO pass from the scene depth, applied to the 3D scene before the game's post-processing (found the same way as the DLSS scene detection). Needs a non-MSAA scene (*Anti-aliasing* off or FSR); with MSAA it is skipped (logged). In this beta it is also off whenever NVIDIA DLSS is selected (log `SSAO disabled: DLSS is the active upscaler`; `RSX_SSAO_DLSS=1` forces it on). `RSX_SSAO_STRENGTH=0..1` (0.6), `RSX_SSAO_RADIUS=0.1..8` (1.0), `RSX_SSAO_DEBUG=1` shows the AO term alone. The game's near plane is not known to the renderer, so the radius is a screen-space approximation.
- **Texture packs** (Compatible renderer). `RSX_TEXDUMP=1` (or `=<folder>`) writes every texture the game uploads to `texdump\`: DXT1/3/5 as DDS (with mipmaps), 32-bit ARGB as PNG, named `<key>_<w>x<h>_<format>_m<mips>`, and lists each one in `texdump\manifest.tsv` (key, size, format, mips, file; textures already listed are not dumped again on the next run). `RSX_TEXPACK=1` (or `=<folder>`) loads replacements from `mods\textures\` (searched recursively; only the first 16 hex digits of the file name, the hash, matter): a DDS in the same DXT format or a PNG, the same size as the original. Missing mip levels are generated (PNG) or kept from the game (DDS; a swizzled texture with missing levels is left unchanged). The key is a 64-bit hash of the texture's content (every mip level, pitch padding excluded) plus its size, format and mip count, never its memory address, so it is the same across runs and in every level. With neither switch set there is no per-upload cost.
- Renderer-side switches (see the bridge): `RSX_ANISO`, `R2_LOD_BIAS`, `R2_HIGHP_Z`, `R2_MSAA`, `R2_GPU_TEX_SCALING`, sharpening.

Not found statically with enough confidence to patch: the game-side shadow-map allocation (render targets are allocated once at boot in a fixed video-memory layout, so shadow resolution is done in the renderer instead), motion blur, depth of field and film grain toggles, particle/decal limits. The game's own debug arguments (`-noeffects`, `-nodecals`, `-shadows` …) only remove things or enable debug views.

## Steam Deck / Linux

The port is a Windows program. On Linux and the Steam Deck it runs through Proton (Wine). This has not been tested on a real Deck yet, so treat it as experimental.

1. Install and build the port on a Windows PC (Setup needs Windows: it uses winget and Visual Studio). No file picker is needed: the installer and Setup find the game on their own, or take a typed path. Then copy the whole port folder to the Deck, for example to `/home/deck/Games/Resistance2Port` (or a microSD card).
2. In Desktop Mode, open Steam → **Games → Add a Non-Steam Game to My Library…**, click **Browse**, and pick `Launcher.exe` in the port folder.
3. Right-click the new entry → **Properties → Compatibility** → tick **Force the use of a specific Steam Play compatibility tool** and choose **Proton Experimental** (or Proton 9.0 or newer).
4. Launch options are not needed. `SteamDeck=1 %command%` forces the Deck preset on other Linux PCs.

What the port does under Proton:

- The launcher detects Proton and the Deck (its hardware line says `Steam Deck` or `Wine / Proton`). On the Deck, **Auto-detect** picks a Deck preset: PS3 resolution, FSR 1 scaling to a 1280x800 window, 30 fps.
- The frame rate list gets a **40** option. Set the Deck's screen to 40 Hz (Quick Access → Performance) to match it. The window list gets **1280 x 800** (the Deck's screen).
- DLSS is not used under Proton; choose FSR instead. The Vulkan renderer runs on the Deck's own Vulkan driver.
- In the launcher, the controller's **A** or **Start** button starts the game. In game, Steam Input presents the Deck's controls as an Xbox controller.
- The update check works the same way and can be turned off in the launcher.

## Status

This is a work in progress, tested on a single PC.

- Runs at a steady 60 fps in most places on my PC; the biggest fights are CPU-bound and can drop to the high 30s, and level streaming can dip briefly lower.
- Performance is mostly limited by the game's SPU job system, so a decent CPU helps most.
- Performance fixes have been applied as far as I realistically could, on the CPU and render-thread side. In the first heli level (1080p internal, DLSS to 4K), the render thread went from about 30 fps to an average of about 54 fps (it was 30 because it waited on ZCULL occlusion queries). It now spends about 9 µs per draw at about 1,250 draws per frame. ZCULL reads no longer stall (`R2_ZCULL=sync` restores the strict behaviour). The game's own main PPU thread and SPU jobs are now the limit.
- The renderer recovers from a corrupt RSX command stream instead of freezing (it logs `FIFO: recovering` to `logs\rpcs3_rsx.log`).
- There are occasional freezes and other bugs.

If you hit a problem, let me know how to reproduce it, and if you can, send the log from the `logs\` folder for that session (the launcher has a button for it). That helps a ton.

## How it works

- `ps3recomp/` is a modified copy of [ps3recomp](https://github.com/sp00nznet/ps3recomp) by sp00nznet (MIT licence). It holds the runtime, the PS3 system libraries (audio, input, file system, SPU scheduling) and the lifters that translate PowerPC and SPU code into C/C++.
- Graphics: the game's RSX command stream is fed to RPCS3's RSX core and Vulkan renderer, built as a library. This repository only contains the patch to RPCS3 (`ps3recomp/patches/r2-rpcs3.patch`); Setup downloads the original source and applies it. The patch adds DLSS / FSR upscaling, display modes and fixes for running outside the emulator.
- `project/` is the game-specific part of the port: its hooks, the 60 fps limiter, mouse look, the launcher and the DLSS bridge.
- `tools/art/make_r2_art.py` draws the installer's wizard artwork (original, Python + Pillow); `tools/installer/make_wizard_images.py` sizes it for Setup.
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
- With DLSS, blob shadows under moving pickups can still trail a little, and some foliage and wire fences can still shimmer.
- **Ambient occlusion is off whenever NVIDIA DLSS is selected** in this beta (also when DLSS falls back to FSR 3.1); it still works with FSR or no upscaler. `RSX_SSAO_DLSS=1` forces it on.
- Heavy fights are CPU-bound (high 30s in the biggest ones on a mid-range CPU). Frame Generation (launcher → Upscaling & Frame Gen) can help here.
- **Frame Generation is experimental and barely tested** (DLSS on RTX 40/50, FSR 3 elsewhere; off by default). Expect ghosting and HUD or menu artifacts; reports are welcome.
- **Changing Frame Generation in-game** can double the sound and then leave constant static. Change it in the launcher, or restart the game after changing it.
- Exclusive fullscreen can give black F12 screenshots. Use borderless.
- Some streaming sound/dialogue files that the EU disc doesn't have show "open FAIL" lines in the log. That's harmless.

## Roadmap

What's planned after this beta, and how it relates to the issues above: [docs/ROADMAP.md](docs/ROADMAP.md).

## Credits and licences

- [Resistance: Fall of Man PC port](https://github.com/Psyykosis/Resistance-Fall-of-Man-Recomp): this port builds on its runtime, tools and research (SPURS/SPU job handling, frame pacing, input, audio and video fixes).
- ps3recomp, by Ned Heller / sp00nznet: MIT.
- SoundTouch 2.4.1 by Olli Parviainen (audio time stretching), in `ps3recomp/third_party/soundtouch/`: LGPL-2.1 (`COPYING.TXT` there).
- at3_standalone, FFmpeg's ATRAC3 / ATRAC3plus decoders made standalone by the PPSSPP project (PPSSPP commit `ebc151f`), in `ps3recomp/third_party/at3_standalone/`: LGPL-2.1-or-later (`COPYING.LGPLv2.1` there). Like SoundTouch it is linked statically; this repository is the source you need to relink it with a modified version.
- Valve Fossilize (Vulkan pipeline recording and replay, `R2_FOSSILIZE`), with RapidJSON, miniz and dirent for Windows, in `ps3recomp/third_party/fossilize/`: MIT (`LICENSE` there, RapidJSON and dirent licences in their folders).
- RPCS3 (RSX core and Vulkan renderer), downloaded during setup and patched: GPL-2.0. Because the built game links RPCS3 code, **the built program is distributed under the GPL-2.0**; the patch in this repository is GPL-2.0 too.
- AMD FidelityFX SDK (FSR 3.1): MIT, © Advanced Micro Devices.
- NVIDIA DLSS SDK v310.9.16, downloaded during setup into `tools/dlss/` only after you accept NVIDIA's licence: NVIDIA RTX SDK licence (`tools/dlss/LICENSE.txt`), not part of this repository. It is used only by the separate DLSS bridge DLL that build.bat builds on your PC. This port uses NVIDIA DLSS; NVIDIA, GeForce RTX and NVIDIA DLSS are trademarks of NVIDIA Corporation.
- Intel XeSS SDK 2.1.1 (runtime `libxess.dll` and headers), downloaded during setup into `tools/xess/` only after you accept Intel's licence, and not part of this repository: Intel Simplified Software License (`tools/xess/LICENSE.txt`, third-party notices in `tools/xess/third-party-programs.txt`). `build.bat` copies the unmodified `libxess.dll` next to the game; reverse engineering it is not allowed. Needs the Visual C++ 2015-2022 runtime (14.40 or newer).
- NVIDIA Streamline SDK 2.14.16 (headers, `sl.*.dll`, `nvngx_dlssg.dll`, `NvLowLatencyVk.dll`), downloaded during setup into `tools/streamline/` only after you accept NVIDIA's licence, and not part of this repository. Headers: MIT (`tools/streamline/license.txt`); the DLLs are NVIDIA RTX SDK / Reflex redistributables (`tools/streamline/nvngx_dlss.license.txt`, `reflex.license.txt`, `3rd-party-licenses.md`). `build.bat` copies them unmodified next to the game.
- FFmpeg 7.1 (the BtbN `win64-lgpl-shared-7.1` build: unmodified `avcodec-61`, `avutil-59` and `swresample-5` DLLs), downloaded during setup into `tools/ffmpeg_bin/` for the game's videos and audio (the H.264, MPEG-2 video and AC-3 decoders): LGPL-3.0-or-later (`tools/ffmpeg_bin/LICENSE.txt`; `tools/ffmpeg_bin/SOURCE.txt` says where it came from and where its source is). It is loaded at run time and is not part of this repository. The FFmpeg headers in `ffmpeg/include/` are LGPL-2.1-or-later (see each file).
- RPCS3's `key_vault.cpp`, downloaded during setup and used only to decrypt your own copy.
- The port's own code: MIT, see `LICENSE`.

## Support

This port is free and always will be: no paid builds, no early access and no perks. It is a fan project made in my spare time.
If you enjoy it and feel like buying me a coffee, it's genuinely appreciated and keeps the late-night debugging going ☕
[buymeacoffee.com/damiro98](https://buymeacoffee.com/damiro98)

A tip supports my own time and tools, such as [shaderscan](https://github.com/miroslavdanutalexandru-alt/shaderscan). It is not a payment for the game or this port, and nothing is unlocked by it. You still need your own copy of the game.
The projects in the credits above, ps3recomp and RPCS3 especially, deserve the support just as much.

*Resistance 2* is © Sony Interactive Entertainment, developed by Insomniac Games. This is an unofficial fan project, not affiliated with or endorsed by Sony or Insomniac. Please own the game you play.
