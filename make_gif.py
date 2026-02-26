#!/usr/bin/env python3
"""Generate a seamless looping GIF from ASCII-Galaxy ANSI output."""

import subprocess, re, math
from PIL import Image, ImageDraw, ImageFont

COLS, ROWS = 100, 35
FRAMES     = 120
STEP       = 2 * math.pi / FRAMES  # exact full rotation → seamless loop
CELL_W, CELL_H = 8, 15
BG_COLOR   = (0, 0, 0)
FONT_PATH  = "/System/Library/Fonts/Menlo.ttc"
FONT_SIZE  = 14
DURATION   = 100  # ms per frame (slower rotation)
OUT_FILE   = "galaxy.gif"

font = ImageFont.truetype(FONT_PATH, FONT_SIZE)

# Run the galaxy binary and capture raw ANSI output
print(f"Running galaxy: {FRAMES} frames, {COLS}x{ROWS}, step={STEP:.6f}")
proc = subprocess.run(
    ["./ASCII-Galaxy/galaxy",
     "--frames", str(FRAMES),
     "--cols",   str(COLS),
     "--rows",   str(ROWS),
     "--step",   f"{STEP:.8f}"],
    capture_output=True, text=True
)
raw = proc.stdout

# Split output into per-frame chunks (each frame starts with ESC[H)
# First chunk before any ESC[H is init garbage (cursor hide etc), skip it
chunks = re.split(r'\x1b\[H', raw)[1:]
# Strip trailing cursor restore from last chunk
chunks = [re.sub(r'\x1b\[\?25[hl]', '', c) for c in chunks]
chunks = [c for c in chunks if c.strip()]
chunks = chunks[:FRAMES]
print(f"Using {len(chunks)} frames")

ansi_color_re = re.compile(r'\x1b\[38;2;(\d+);(\d+);(\d+)m(.)')
ansi_reset_re = re.compile(r'\x1b\[0m')

def parse_frame(text):
    """Parse one frame of ANSI output into a grid of (char, r, g, b)."""
    grid = [[('' , 0, 0, 0)] * COLS for _ in range(ROWS)]
    lines = text.split('\n')
    for y, line in enumerate(lines[:ROWS]):
        x = 0
        pos = 0
        while pos < len(line) and x < COLS:
            # Try ANSI color sequence
            m = ansi_color_re.match(line, pos)
            if m:
                r, g, b, ch = int(m.group(1)), int(m.group(2)), int(m.group(3)), m.group(4)
                grid[y][x] = (ch, r, g, b)
                x += 1
                pos = m.end()
                continue
            # Try reset sequence
            m2 = ansi_reset_re.match(line, pos)
            if m2:
                pos = m2.end()
                continue
            # Try other ESC sequences (skip)
            if line[pos] == '\x1b':
                end = line.find('m', pos)
                if end != -1:
                    pos = end + 1
                    continue
            # Plain character
            ch = line[pos]
            grid[y][x] = (ch, 200, 200, 200) if ch != ' ' else (' ', 0, 0, 0)
            x += 1
            pos += 1
    return grid

def render_frame(grid):
    """Render a parsed grid to a PIL Image."""
    img = Image.new('RGB', (COLS * CELL_W, ROWS * CELL_H), BG_COLOR)
    draw = ImageDraw.Draw(img)
    for y in range(ROWS):
        for x in range(COLS):
            ch, r, g, b = grid[y][x]
            if ch and ch != ' ':
                draw.text((x * CELL_W, y * CELL_H), ch,
                          font=font, fill=(r, g, b))
    return img

# Generate all frames
images = []
for i, chunk in enumerate(chunks):
    grid = parse_frame(chunk)
    img  = render_frame(grid)
    images.append(img)
    if (i + 1) % 20 == 0:
        print(f"  rendered {i+1}/{len(chunks)}")

print(f"Saving {OUT_FILE} ({len(images)} frames)...")
images[0].save(
    OUT_FILE,
    save_all=True,
    append_images=images[1:],
    duration=DURATION,
    loop=0,
    optimize=False
)

import os
size_mb = os.path.getsize(OUT_FILE) / (1024 * 1024)
print(f"Done: {OUT_FILE} ({size_mb:.1f} MB)")
