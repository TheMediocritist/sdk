#!/usr/bin/env python3
# Load image from file

import re
import sys

from PIL import Image

if len(sys.argv) < 2:
    print("Використання: image2code.py [--rle | --mono] зображення.png [зображення_2.png] ...")
    sys.exit(1)

print(f"Завантажуємо {sys.argv[1]}... ", end="")

# Pop flags from argv
i = 1
use_rle = False
use_mono = False
while i < len(sys.argv):
    if sys.argv[i].startswith("-"):
        arg = sys.argv.pop(i)
        if arg == "--rle":
            use_rle = True
        elif arg == "--mono":
            use_mono = True
    else:
        i += 1

if use_rle and use_mono:
    print("Помилка: --rle і --mono не можна використовувати разом.")
    sys.exit(1)

for fname in sys.argv[1:]:
    # Always normalize to RGBA up front so getpixel() is reliable regardless
    # of the source file's original mode (RGB, P, LA, ...) - avoids the
    # fragile "r, g, b, *_ = getpixel(...)" pattern for images with no
    # alpha channel.
    img = Image.open(fname).convert("RGBA")

    print(f"Розмір: {img.width}x{img.height}")

    out = fname.rpartition(".")[0] + ".h"
    var_name = re.sub(
        r"[^a-zA-Z0-9_]", "_", fname.rpartition(".")[0].rpartition("/")[2]
    )

    if use_mono:
        # Packed 1bpp color plane + 1bpp mask plane. Row-major, MSB-first,
        # stride = ceil(width/8) bytes per row per plane - matches every
        # other 1bpp bit layout in the firmware (MonoCanvas framebuffer,
        # blitMono, the menu's own cursor-row compositing). A pixel is
        # drawn white if its color bit is set AND its mask bit is set;
        # mask bit 0 means fully transparent (skip - background shows
        # through), matching the source PNG's actual alpha channel rather
        # than a magic transparent-color convention.
        #
        # Color threshold matches the runtime's own RGB565->mono rule
        # exactly (any channel >= 50% of its range -> white): in 8-bit
        # terms that's channel >= 128, equivalent to RGB565's
        # "top bit of R, G, or B set" after >>3/>>2/>>3 truncation.
        # Alpha threshold: >= 128 -> opaque.
        stride = (img.width + 7) // 8
        color_bytes = bytearray(stride * img.height)
        mask_bytes = bytearray(stride * img.height)

        for y in range(img.height):
            for x in range(img.width):
                r, g, b, a = img.getpixel((x, y))
                byte_i = y * stride + (x >> 3)
                bit = 0x80 >> (x & 7)
                if r >= 128 or g >= 128 or b >= 128:
                    color_bytes[byte_i] |= bit
                if a >= 128:
                    mask_bytes[byte_i] |= bit

        with open(out, "w") as f:
            print(f"Записуємо mono (1bpp+mask) дані у {out}... ", end="")
            print("#pragma once", file=f)
            print("// This is a generated file, do not edit.", file=f)
            print("// clang-format off", file=f)
            print("#include <stdint.h>", file=f)
            print('#include "lilka/ui.h"  // MonoIconAsset', file=f)
            print(f"const uint16_t {var_name}_img_width = {img.width};", file=f)
            print(f"const uint16_t {var_name}_img_height = {img.height};", file=f)
            print(f"static const uint8_t {var_name}_img_color[] = {{", file=f)
            for i in range(0, len(color_bytes), 12):
                row = color_bytes[i : i + 12]
                print("    " + ", ".join(f"0x{b:02x}" for b in row) + ",", file=f)
            print("};", file=f)
            print(f"static const uint8_t {var_name}_img_mask[] = {{", file=f)
            for i in range(0, len(mask_bytes), 12):
                row = mask_bytes[i : i + 12]
                print("    " + ", ".join(f"0x{b:02x}" for b in row) + ",", file=f)
            print("};", file=f)
            print(
                f"const menu_icon_t {var_name}_img = "
                f"{{{img.width}, {img.height}, {var_name}_img_color, {var_name}_img_mask}};",
                file=f,
            )
            print("// clang-format on", file=f)
        print(
            f"Готово! RGB565 було б {img.width * img.height * 2} байтів, "
            f"mono: {len(color_bytes) + len(mask_bytes)} байтів."
        )
        continue

    pixels = []

    # Iterate through each pixel
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, _a = img.getpixel((x, y))

            # Convert to RGB-565
            r = r >> 3
            g = g >> 2
            b = b >> 3

            pixels.append((r << 11) | (g << 5) | b)

    if use_rle:
        # Encode data with RLE
        encoded = bytearray()
        i = 0
        while i < len(pixels):
            count = 1
            while (
                i + count < len(pixels)
                and count < 255
                and pixels[i] == pixels[i + count]
            ):
                count += 1
            encoded.append(count)
            encoded.append(pixels[i] & 0xFF)
            encoded.append(pixels[i] >> 8)
            i += count
        print(f"RLE: стиснуто {len(pixels) * 2} байтів до {len(encoded)} байтів")
        if len(encoded) > len(pixels * 2):
            print(
                "RLE: УВАГА! Стиснення не оптимальне! Вихідний файл буде більшим, ніж без стиснення!"
            )
        with open(out, "w") as f:
            print('Записуємо стиснені дані у "{}"... '.format(out), end="")
            print("#pragma once", file=f)
            print("// This is a generated file, do not edit.", file=f)
            print("// clang-format off", file=f)
            print(f"#include <stdint.h>", file=f)
            print(f"const uint32_t {var_name}_length = {len(encoded)};", file=f)
            print(f"const uint8_t {var_name}[] = {{", file=f)
            for byte in encoded:
                print(f"    0x{byte:02x},", file=f)
            print("};", file=f)
            print("// clang-format on", file=f)
            print("Зроблено!")

    else:
        with open(out, "w") as f:
            print(f"Записуємо {len(pixels)} пікселів у {out}... ", end="")
            print("#pragma once", file=f)
            print("// This is a generated file, do not edit.", file=f)
            print("// clang-format off", file=f)
            print(f"#include <stdint.h>", file=f)
            print(f"const uint16_t {var_name}_img_width = {img.width};", file=f)
            print(f"const uint16_t {var_name}_img_height = {img.height};", file=f)
            print(f"const uint16_t {var_name}_img[] = {{", file=f)
            for pixel in pixels:
                print(f"    0x{pixel:04x},", file=f)
            print("};", file=f)
            print("// clang-format on", file=f)
            print("Зроблено!")
