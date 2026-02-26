# ASCII Galaxy

3D spiral galaxy animation rendered in ASCII with true color ANSI, coded in Cpp.

Built by adapting the rendering technique from [a1k0n's donut.c](https://www.a1k0n.net/2006/09/15/obfuscated-c-donut.html)

![ASCII Galaxy Animation](galaxy.gif)

## How It Works

1. **Galaxy model** — ~73K points sampled in polar coordinates. Brightness at each point is determined by logarithmic spiral arm density, a Gaussian central bulge, dust lane absorption, and scattered HII (star-forming) regions.

2. **3D rendering** — Each frame, all points are rotated (spin + tilt) using rotation matrices, then perspective-projected to screen coordinates: `x' = K1·x/(K2+z)`.

3. **Additive accumulation** — Unlike the donut's z-buffer (opaque surface), the galaxy uses additive blending: overlapping points sum their brightness, so denser regions naturally glow brighter.

4. **Color** — Each pixel blends warm gold (bulge), cool blue (spiral arms), and pink (HII regions) via ANSI 24-bit true color escape codes.

5. **Auto-resize** — Detects terminal size and adapts on window resize (`SIGWINCH`), rotating at ~30 fps.

## Reference

- [Donut math: how donut.c works](https://www.a1k0n.net/2006/09/15/obfuscated-c-donut.html) — the original technique this project is based on
