# Roadmap

What's planned after the 0.2.0 beta, roughly in priority order. Nothing here has a date; priorities depend on what testers report.

## Stability

- Find and fix the remaining causes of the occasional freezes.
- Test levels past the opening Iceland levels, using testers' reports and logs.

## Visual quality

- Make ambient occlusion correct with DLSS, so it can be turned back on.
- Reduce DLSS shimmer on wire fences and foliage, and the trailing on blob shadows under moving pickups.

## Performance

- Cheaper DLSS object motion vectors (about 3–4 fps in heavy fights).
- Split the renderer's command decoding and Vulkan recording across threads, the bigger fix for CPU-bound fights.

## Features

- Ultrawide: keep the HUD and menus in a centred 16:9 area reliably (the view is already Hor+; the *Ultrawide HUD* option is experimental).
- Online play through community servers (re-psn / Horizon).
- Possibly one launcher for all three Resistance ports.

## Ports in the series

- [Resistance: Fall of Man](https://github.com/miroslavdanutalexandru-alt/Resistance-Fall-of-Man-Recomp)
- Resistance 2 (this repository)
- Resistance 3
