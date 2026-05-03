"""
Конвертирует TTF шрифт в U8g2 формат с поддержкой кириллицы и знака °.

Шаги:
1. Рендерит глифы из TTF через freetype-py
2. Генерирует BDF файл
3. Вызывает bdfconv для создания U8g2 C-массива

Использование:
  python3 generate_u8g2_font.py

Результат: inkcast/rubik_fonts_u8g2.h
"""

import freetype
import subprocess
import os
import sys
import tempfile

FONT_PATH = os.path.join(os.path.dirname(__file__), "fonts", "Rubik-Variable.ttf")
BDFCONV = "/tmp/u8g2/tools/font/bdfconv/bdfconv"
OUTPUT_DIR = os.path.join(os.path.dirname(__file__), "..", "inkcast")

# Диапазоны Unicode для включения в шрифт
# ASCII (0x20-0x7E) + ° (0xB0) + Кириллица (0x410-0x44F) + Ё/ё (0x401, 0x451)
UNICODE_RANGES = "32-126,176,1025,1040-1103,1105"

# Вес шрифта для переменного TTF (ось Weight: 300-900)
# 300=Light, 400=Regular, 500=Medium, 600=SemiBold, 700=Bold
FONT_WEIGHT = 500  # Medium — равномерные штрихи на монохромном e-ink

# Размеры шрифтов для генерации
FONTS = [
    {"name": "rubik_u8g2_14",  "size": 14},
    {"name": "rubik_u8g2_20",  "size": 20},
    {"name": "rubik_u8g2_24",  "size": 24},
    {"name": "rubik_u8g2_42",  "size": 42},
]


def set_font_weight(face, weight):
    """Устанавливает вес для переменного шрифта через FreeType API"""
    from ctypes import byref, POINTER, c_long
    coords = (freetype.FT_Fixed * 1)(int(weight * 65536))
    freetype.FT_Set_Var_Design_Coordinates(face._FT_Face, 1, coords)


def ttf_to_bdf(ttf_path, pixel_size, bdf_path):
    """Конвертирует TTF в BDF используя freetype-py"""
    face = freetype.Face(ttf_path)
    set_font_weight(face, FONT_WEIGHT)
    face.set_pixel_sizes(0, pixel_size)

    # Собираем все нужные кодпоинты
    codepoints = set()
    for part in UNICODE_RANGES.split(","):
        if "-" in part:
            start, end = part.split("-")
            codepoints.update(range(int(start), int(end) + 1))
        else:
            codepoints.add(int(part))

    # Определяем метрики шрифта
    ascent = face.size.ascender >> 6
    descent = -(face.size.descender >> 6)
    height = ascent + descent
    max_width = 0

    # Предварительный проход: находим max_width и фильтруем доступные глифы
    available = {}
    for cp in sorted(codepoints):
        glyph_index = face.get_char_index(cp)
        if glyph_index == 0 and cp != 32:
            continue
        face.load_char(chr(cp), freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO)
        glyph = face.glyph
        bitmap = glyph.bitmap

        w = bitmap.width
        h = bitmap.rows
        advance = glyph.advance.x >> 6
        bearing_x = glyph.bitmap_left
        bearing_y = glyph.bitmap_top

        if advance > max_width:
            max_width = advance

        # Конвертируем bitmap в строки hex
        hex_rows = []
        bytes_per_row = (w + 7) // 8
        for row in range(h):
            row_bytes = []
            for b in range(bytes_per_row):
                byte_val = 0
                for bit in range(8):
                    col = b * 8 + bit
                    if col < w:
                        pixel_byte = bitmap.buffer[row * bitmap.pitch + col // 8]
                        if pixel_byte & (0x80 >> (col % 8)):
                            byte_val |= (0x80 >> bit)
                row_bytes.append(byte_val)
            hex_rows.append("".join(f"{b:02X}" for b in row_bytes))

        available[cp] = {
            "width": w, "height": h,
            "advance": advance,
            "bearing_x": bearing_x, "bearing_y": bearing_y,
            "hex_rows": hex_rows,
        }

    # Пишем BDF
    font_name = f"Rubik-{pixel_size}"
    with open(bdf_path, "w") as f:
        f.write(f"STARTFONT 2.1\n")
        f.write(f"FONT -{font_name}\n")
        f.write(f"SIZE {pixel_size} 72 72\n")
        f.write(f"FONTBOUNDINGBOX {max_width} {height} 0 {-descent}\n")
        f.write(f"STARTPROPERTIES 4\n")
        f.write(f"FONT_ASCENT {ascent}\n")
        f.write(f"FONT_DESCENT {descent}\n")
        f.write(f"DEFAULT_CHAR 32\n")
        f.write(f"PIXEL_SIZE {pixel_size}\n")
        f.write(f"ENDPROPERTIES\n")
        f.write(f"CHARS {len(available)}\n")

        for cp, g in sorted(available.items()):
            char_name = f"U+{cp:04X}"
            bbx_y_offset = g["bearing_y"] - g["height"]
            f.write(f"STARTCHAR {char_name}\n")
            f.write(f"ENCODING {cp}\n")
            f.write(f"SWIDTH {g['advance'] * 1000 // pixel_size} 0\n")
            f.write(f"DWIDTH {g['advance']} 0\n")
            f.write(f"BBX {g['width']} {g['height']} {g['bearing_x']} {bbx_y_offset}\n")
            f.write(f"BITMAP\n")
            for row_hex in g["hex_rows"]:
                f.write(f"{row_hex}\n")
            f.write(f"ENDCHAR\n")

        f.write(f"ENDFONT\n")

    print(f"  BDF: {len(available)} глифов, ascent={ascent}, descent={descent}")


def bdf_to_u8g2(bdf_path, font_name):
    """Конвертирует BDF в U8g2 C-массив через bdfconv"""
    # -f 1: font format 1 (full)
    # -m: unicode map
    # -n: font name
    map_str = UNICODE_RANGES
    cmd = [
        BDFCONV, bdf_path,
        "-f", "1",
        "-m", map_str,
        "-n", font_name,
        "-o", "/dev/stdout"
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  ОШИБКА bdfconv: {result.stderr}")
        return None
    return result.stdout


def main():
    if not os.path.exists(BDFCONV):
        print(f"bdfconv не найден: {BDFCONV}")
        print("Собери: cd /tmp/u8g2/tools/font/bdfconv && make")
        sys.exit(1)

    if not os.path.exists(FONT_PATH):
        print(f"Шрифт не найден: {FONT_PATH}")
        sys.exit(1)

    print(f"Шрифт: {FONT_PATH}")
    print(f"Unicode: {UNICODE_RANGES}")
    print()

    h_lines = [
        "#pragma once",
        "",
        "// Rubik шрифты в формате U8g2 (ASCII + кириллица + °)",
        f"// Сгенерировано из {os.path.basename(FONT_PATH)}",
        "// Диапазоны: 0x20-0x7E, 0xB0(°), 0x401(Ё), 0x410-0x44F(А-я), 0x451(ё)",
        "",
    ]

    for font_cfg in FONTS:
        name = font_cfg["name"]
        size = font_cfg["size"]
        print(f"[{name}] {size}px...")

        with tempfile.NamedTemporaryFile(suffix=".bdf", delete=False) as tmp:
            bdf_path = tmp.name

        try:
            ttf_to_bdf(FONT_PATH, size, bdf_path)
            c_data = bdf_to_u8g2(bdf_path, name)
            if c_data:
                h_lines.append(c_data)
                h_lines.append("")
                print(f"  OK")
            else:
                print(f"  ПРОПУЩЕН")
        finally:
            os.unlink(bdf_path)

    output_path = os.path.join(OUTPUT_DIR, "rubik_fonts_u8g2.h")
    with open(output_path, "w", encoding="utf-8") as f:
        f.write("\n".join(h_lines))
    print(f"\nГотово: {output_path}")


if __name__ == "__main__":
    main()
