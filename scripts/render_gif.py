#!/usr/bin/env python3
"""Render an asciinema v2 cast into a GIF with a minimal terminal emulator.
Full-redraw frames: no ghosting. CJK double-width aware."""
import json, sys
from PIL import Image, ImageDraw, ImageFont

CAST = sys.argv[1]
OUT = sys.argv[2] if len(sys.argv) > 2 else "/tmp/demo.gif"

WIDTH, HEIGHT = 110, 36
CELL_W, CELL_H = 18, 26
SPEED = 10.0
IDLE_CAP = 0.6
BG = (24, 26, 32)
FG = (220, 223, 228)
MARGIN = 14

font = ImageFont.truetype("/root/.fonts/wqy-microhei.ttc", 17)

EMOJI_MAP = {"\u2705": "\u221a", "\u2714": "\u221a", "\u26a0": "!",
             "\U0001f7e2": "\u25cf", "\U0001f44d": "^", "\U0001f4c8": "~",
             "\U0001f4dd": "*", "\U0001f30f": "*", "\U0001f4a1": "*",
             "\ufe0f": "", "\u200d": ""}

def char_w(ch):
    return 2 if ord(ch) > 0x2E7F else 1

def replay():
    lines = open(CAST).read().splitlines()
    grid = [[" "] * WIDTH for _ in range(HEIGHT)]
    state = {"crow": 0, "ccol": 0}
    frames = []
    t_prev = 0.0

    def scroll():
        grid.pop(0)
        grid.append([" "] * WIDTH)
        state["crow"] = HEIGHT - 1

    def snapshot(t):
        frames.append((t, ["".join(r) for r in grid]))

    for raw in lines[1:]:
        ev = json.loads(raw)
        t, etype, data = ev[0], ev[1], ev[2]
        if etype != "o":
            continue
        for k, v in EMOJI_MAP.items():
            data = data.replace(k, v)
        i = 0
        while i < len(data):
            ch = data[i]
            if ch == "\x1b":
                if data[i:i + 4] == "\x1b[2K":
                    grid[state["crow"]] = [" "] * WIDTH
                    i += 4
                    continue
                i += 1
                continue
            if ch == "\r":
                state["ccol"] = 0
            elif ch == "\n":
                state["ccol"] = 0
                state["crow"] += 1
                if state["crow"] >= HEIGHT:
                    scroll()
            elif ord(ch) < 32:
                pass
            else:
                w = char_w(ch)
                if state["ccol"] + w > WIDTH:
                    state["ccol"] = 0
                    state["crow"] += 1
                    if state["crow"] >= HEIGHT:
                        scroll()
                grid[state["crow"]][state["ccol"]] = ch
                state["ccol"] += 1
                if w == 2 and state["ccol"] < WIDTH:
                    grid[state["crow"]][state["ccol"]] = ""
                    state["ccol"] += 1
            i += 1
        dt = (t - t_prev) / SPEED
        if dt > IDLE_CAP:
            dt = IDLE_CAP
        if dt >= 0.1:
            snapshot(frames[-1][0] + dt if frames else t / SPEED)
        t_prev = t
    snapshot(frames[-1][0] + 3.0 if frames else 3.0)
    return frames

frames = replay()
print("frames:", len(frames), "duration: %.1fs" % frames[-1][0])

W = MARGIN * 2 + WIDTH * CELL_W
H = MARGIN * 2 + HEIGHT * CELL_H
adv_latin, adv_cjk = 9, 18
prev_t = 0.0
imgs, durs = [], []
for t, rows in frames:
    im = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(im)
    y = MARGIN
    for row in rows:
        x = MARGIN
        for ch in row:
            if ch and ch != " ":
                d.text((x, y), ch, font=font, fill=FG)
            x += adv_cjk if char_w(ch) == 2 else adv_latin
        y += CELL_H
    durs.append(max(0.1, round(t - prev_t, 2)))
    prev_t = t
    imgs.append(im)

kept_i, kept_d = [], []
for i, im in enumerate(imgs):
    if kept_i and im.tobytes() == kept_i[-1].tobytes():
        kept_d[-1] += durs[i]
        continue
    kept_i.append(im)
    kept_d.append(durs[i])

pal = [im.convert("P", palette=Image.ADAPTIVE, colors=8) for im in kept_i]
pal[0].save(OUT, save_all=True, append_images=pal[1:],
            duration=[max(0.1, d) for d in kept_d], loop=0, optimize=True)
print("saved", OUT, "kept frames:", len(pal))
