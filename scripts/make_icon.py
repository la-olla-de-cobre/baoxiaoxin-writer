#!/usr/bin/env python3
"""生成 res/app.ico（多尺寸应用图标）。

设计：Indigo 圆角底 + 键帽面 + 文字光标，配色与界面 CLR_ACCENT 一致。
小尺寸（<32px）省略两侧小方块，避免在托盘里糊成噪点。

用法：python scripts/make_icon.py
"""

import io
import os
import struct

from PIL import Image, ImageDraw

# 与 src/ui.h 的 CLR_ACCENT = RGB(99, 102, 241) 保持一致
INDIGO = (99, 102, 241, 255)
WHITE = (255, 255, 255, 255)
PLATE = (255, 255, 255, 56)
DOT = (255, 255, 255, 217)

SIZES = [16, 20, 24, 32, 48, 64, 128, 256]
SUPERSAMPLE = 8


def draw_icon(size):
    """按归一化坐标绘制，超采样后再缩小以获得干净的抗锯齿。"""
    span = size * SUPERSAMPLE
    img = Image.new("RGBA", (span, span), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    def rrect(x0, y0, x1, y1, radius, fill):
        draw.rounded_rectangle(
            [x0 * span, y0 * span, x1 * span - 1, y1 * span - 1],
            radius=radius * span,
            fill=fill,
        )

    rrect(0.02, 0.02, 0.98, 0.98, 0.22, INDIGO)   # 圆角底
    rrect(0.26, 0.24, 0.74, 0.76, 0.09, PLATE)    # 键帽面
    rrect(0.45, 0.30, 0.55, 0.70, 0.03, WHITE)    # 文字光标

    if size >= 32:                                 # 小尺寸省略，防止糊成噪点
        rrect(0.33, 0.30, 0.43, 0.40, 0.03, DOT)
        rrect(0.57, 0.30, 0.67, 0.40, 0.03, DOT)

    return img.resize((size, size), Image.LANCZOS)


def build_ico(images):
    """手工组装 ICO（PNG 负载），以便每个尺寸使用各自绘制的版本。

    PIL 的 ICO 保存会自行缩放同一张图，无法保留按尺寸简化的设计，
    所以这里直接写容器格式。
    """
    blobs = []
    for img in images:
        buf = io.BytesIO()
        img.save(buf, format="PNG", optimize=True)
        blobs.append(buf.getvalue())

    count = len(images)
    offset = 6 + 16 * count

    out = bytearray()
    out += struct.pack("<HHH", 0, 1, count)        # ICONDIR: reserved, type=icon, count
    for img, blob in zip(images, blobs):
        w = 0 if img.width >= 256 else img.width   # 256 在该字段中记为 0
        h = 0 if img.height >= 256 else img.height
        out += struct.pack(
            "<BBBBHHII",
            w, h,
            0,          # 调色板颜色数（真彩为 0）
            0,          # 保留
            1,          # 色彩平面
            32,         # 位深
            len(blob),
            offset,
        )
        offset += len(blob)
    for blob in blobs:
        out += blob
    return bytes(out)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    target = os.path.join(root, "res", "app.ico")

    images = [draw_icon(size) for size in SIZES]
    with open(target, "wb") as handle:
        handle.write(build_ico(images))

    print("wrote %s (%d bytes)" % (target, os.path.getsize(target)))
    print("sizes: %s" % ", ".join("%dx%d" % (s, s) for s in SIZES))


if __name__ == "__main__":
    main()
