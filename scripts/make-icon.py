#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 KC2RGW Chris
#
# Makes 590Ctrl's icon: resources/590Ctrl.svg, and from it 590Ctrl.png (512 px, the
# window icon and the Linux packages'), 590Ctrl.icns (macOS) and 590Ctrl.ico (Windows).
# The style is the family's (SimpleHPSDR, SimpleDream, PatchPrimeMac): a dark slate
# tile with a faint grid, blue, cyan and yellow. The picture is the radio's front
# panel in two parts: a big tuning knob under an S-meter arc.
#
#   scripts/make-icon.py
#
# Needs rsvg-convert (brew install librsvg), ImageMagick and iconutil (macOS).

import math
import os
import shutil
import subprocess
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "resources")

TILE = (100, 100, 824, 185)          # x, y, size, corner radius
CX, CY = 512, 660                    # the knob's centre, and the meter's
KNOB = 200                           # the knob's radius
SEGMENTS = 15
LIT = 10                             # the S-meter reads S9 or so
SEG_COLOURS = ["#123a8c", "#123a8c", "#106BD8", "#106BD8", "#106BD8", "#106BD8", "#2ED3C8", "#2ED3C8",
               "#2ED3C8", "#F5D03B", "#F5D03B", "#F9DF83", "#F9DF83", "#F9DF83", "#F9DF83"]


def point(r, deg):
    a = math.radians(deg)
    return CX + r * math.cos(a), CY - r * math.sin(a)


def svg():
    x0, y0, size, r = TILE
    p = [f'''<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="0 0 1024 1024">
<defs>
<linearGradient id="tile" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#1F2633"/><stop offset="1" stop-color="#0B0D11"/></linearGradient>
<linearGradient id="glow" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#3B82F6" stop-opacity="0.0"/><stop offset="1" stop-color="#3B82F6" stop-opacity="0.18"/></linearGradient>
<linearGradient id="rim" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#4A5568"/><stop offset="1" stop-color="#151A22"/></linearGradient>
<radialGradient id="face" cx="0.42" cy="0.35" r="0.75"><stop offset="0" stop-color="#3A4352"/><stop offset="0.6" stop-color="#1D232D"/><stop offset="1" stop-color="#11151B"/></radialGradient>
<radialGradient id="dimple" cx="0.6" cy="0.65" r="0.7"><stop offset="0" stop-color="#2B3240"/><stop offset="1" stop-color="#0A0D12"/></radialGradient>
<clipPath id="c"><rect x="{x0}" y="{y0}" width="{size}" height="{size}" rx="{r}"/></clipPath>
</defs>
<rect x="{x0}" y="{y0}" width="{size}" height="{size}" rx="{r}" fill="url(#tile)"/>
<g clip-path="url(#c)">
<rect x="{x0}" y="{y0 + size // 2}" width="{size}" height="{size // 2}" fill="url(#glow)"/>
<g stroke="#FFFFFF" stroke-opacity="0.06" stroke-width="3">''']
    for y in (300, 512, 724):
        p.append(f'<line x1="{x0}" y1="{y}" x2="{x0 + size}" y2="{y}"/>')
    p.append("</g>")
    # The S-meter: segments along an arc over the knob, from lower left to lower right.
    start, end = 160, 20
    inner, outer = KNOB + 70, KNOB + 150
    for i in range(SEGMENTS):
        deg = start - (start - end) * (i + 0.5) / SEGMENTS
        lit = i < LIT
        colour = SEG_COLOURS[i] if lit else "#1A2130"
        mid = (inner + outer) / 2
        cx, cy = point(mid, deg)
        length = outer - inner
        width = 2 * math.pi * mid * ((start - end) / 360) / SEGMENTS - 12
        rot = 90 - deg
        p.append(f'<rect x="{cx - width / 2:.1f}" y="{cy - length / 2:.1f}" width="{width:.1f}" height="{length}" rx="9" '
                 f'fill="{colour}" opacity="{0.95 if lit else 0.9}" transform="rotate({rot:.1f} {cx:.1f} {cy:.1f})"/>')
    # Its scale: a thin arc inside the segments.
    a1, a2 = point(inner - 22, start), point(inner - 22, end)
    p.append(f'<path d="M{a1[0]:.1f},{a1[1]:.1f} A{inner - 22},{inner - 22} 0 0 1 {a2[0]:.1f},{a2[1]:.1f}" fill="none" '
             f'stroke="#7FB2FF" stroke-opacity="0.35" stroke-width="5" stroke-linecap="round"/>')
    # The knob: a rim, the knurled edge, the face, a finger dimple and its mark.
    p.append(f'<circle cx="{CX}" cy="{CY}" r="{KNOB + 10}" fill="#05070A" opacity="0.6"/>')
    p.append(f'<circle cx="{CX}" cy="{CY}" r="{KNOB}" fill="url(#rim)"/>')
    for k in range(72):
        x1, y1 = point(KNOB - 2, k * 5)
        x2, y2 = point(KNOB - 26, k * 5)
        p.append(f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" stroke="#000000" stroke-opacity="0.35" stroke-width="4"/>')
    p.append(f'<circle cx="{CX}" cy="{CY}" r="{KNOB - 30}" fill="url(#face)" stroke="#FFFFFF" stroke-opacity="0.08" stroke-width="3"/>')
    dx, dy = point(KNOB - 95, 125)
    p.append(f'<circle cx="{dx:.1f}" cy="{dy:.1f}" r="40" fill="url(#dimple)" stroke="#FFFFFF" stroke-opacity="0.10" stroke-width="3"/>')
    mx1, my1 = point(KNOB - 48, 55)
    mx2, my2 = point(KNOB - 12, 55)
    p.append(f'<line x1="{mx1:.1f}" y1="{my1:.1f}" x2="{mx2:.1f}" y2="{my2:.1f}" stroke="#3B82F6" stroke-width="14" stroke-linecap="round"/>')
    p.append("</g>")
    p.append(f'<rect x="{x0 + 0.5}" y="{y0 + 0.5}" width="{size - 1}" height="{size - 1}" rx="{r}" fill="none" stroke="#FFFFFF" stroke-opacity="0.09" stroke-width="2"/>')
    p.append("</svg>\n")
    return "\n".join(p)


def run(args):
    subprocess.run(args, check=True, capture_output=True)


def main():
    svg_path = os.path.join(OUT, "590Ctrl.svg")
    with open(svg_path, "w") as f:
        f.write(svg())
    work = tempfile.mkdtemp()
    big = os.path.join(work, "1024.png")
    run(["rsvg-convert", "-w", "1024", "-h", "1024", "-o", big, svg_path])
    run(["magick", big, "-resize", "512x512", "-strip", os.path.join(OUT, "590Ctrl.png")])
    iconset = os.path.join(work, "590Ctrl.iconset")
    os.makedirs(iconset)
    for size in (16, 32, 128, 256, 512):
        for scale in (1, 2):
            px = size * scale
            run(["magick", big, "-resize", f"{px}x{px}", "-strip",
                 os.path.join(iconset, f"icon_{size}x{size}{'@2x' if scale == 2 else ''}.png")])
    run(["iconutil", "-c", "icns", iconset, "-o", os.path.join(OUT, "590Ctrl.icns")])
    run(["magick", big, "-strip", "-define", "icon:auto-resize=256,128,64,48,32,24,16", os.path.join(OUT, "590Ctrl.ico")])
    shutil.rmtree(work)
    print("made", svg_path, "590Ctrl.png, .icns, .ico")


if __name__ == "__main__":
    main()
