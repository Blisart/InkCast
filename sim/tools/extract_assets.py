#!/usr/bin/env python3
"""Извлечение ассетов из исходников прошивки для симулятора дисплея.

Запускается разово (или после правки шрифтов/иконок/layout в прошивке):

    cd sim
    python tools/extract_assets.py

На выходе — JSON в sim/src/assets/:
  - icons.json         иконки 16/24/28/32/48 px из icons.h
  - weather_icons.json иконки 96/64 px + маппинг OWM id/code → icon name
  - layout.json        константы зон дисплея из display_renderer.cpp
  - fonts.json         глифы Rubik (TODO в следующем коммите)
"""
from __future__ import annotations

import base64
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIRMWARE_DIR = ROOT / "inkcast"
ASSETS_DIR = ROOT / "sim" / "src" / "assets"

ICONS_H = FIRMWARE_DIR / "icons.h"
WEATHER_ICONS_H = FIRMWARE_DIR / "weather_icons.h"
RENDERER_CPP = FIRMWARE_DIR / "display_renderer.cpp"
CONFIG_H = FIRMWARE_DIR / "config.h"
TTF_PATH = ROOT / "tools" / "fonts" / "Rubik-Variable.ttf"

# Размеры шрифтов должны совпадать с номерами в rubik_fonts_u8g2.h
# (rubik_u8g2_14 / 20 / 24 / 42 — пиксельная высота).
FONT_SIZES = [14, 20, 24, 42]

# Диапазоны codepoint'ов, которые использует прошивка:
#   ASCII 0x20..0x7E (печатные + пробел)
#   0xB0              — знак градуса (°)
#   0x401             — Ё
#   0x410..0x44F      — А..я
#   0x451             — ё
FONT_CODEPOINTS: list[int] = (
    list(range(0x20, 0x7F))
    + [0xB0]
    + [0x401]
    + list(range(0x410, 0x450))
    + [0x451]
)


# ────────────────────────────────────────────────────────────────
# Парсинг битмап-массивов вида
#   [static] const unsigned char NAME[] PROGMEM = { 0x00, 0x01, ... };
# ────────────────────────────────────────────────────────────────

BITMAP_RE = re.compile(
    r"(?:static\s+)?const\s+unsigned\s+char\s+(\w+)\s*\[\]\s*PROGMEM\s*=\s*\{([^}]*)\};",
    re.MULTILINE | re.DOTALL,
)
HEX_BYTE_RE = re.compile(r"0x([0-9A-Fa-f]{1,2})")


@dataclass
class Bitmap:
    name: str
    width: int
    height: int
    data: bytes

    def to_json(self) -> dict[str, int | str]:
        return {
            "w": self.width,
            "h": self.height,
            "b64": base64.b64encode(self.data).decode("ascii"),
        }


# Переопределения размера для нестандартных иконок
# (имя содержит размер, но картинка не квадратная)
DIM_OVERRIDES: dict[str, tuple[int, int]] = {
    "icon28_sun": (32, 28),  # реально 32×28, в drawBitmap передаётся 32, 28
}

# Префикс → (ширина, высота) для квадратных иконок
PREFIX_DIMS: dict[str, tuple[int, int]] = {
    "icon16_": (16, 16),
    "icon24_": (24, 24),
    "icon32_": (32, 32),
    "icon48_": (48, 48),
    "icon64_": (64, 64),
    "icon96_": (96, 96),
}


def dims_for(name: str) -> tuple[int, int] | None:
    if name in DIM_OVERRIDES:
        return DIM_OVERRIDES[name]
    for prefix, dims in PREFIX_DIMS.items():
        if name.startswith(prefix):
            return dims
    return None


def parse_bitmaps(source: str) -> list[Bitmap]:
    bitmaps: list[Bitmap] = []
    for match in BITMAP_RE.finditer(source):
        name = match.group(1)
        raw = match.group(2)
        data = bytes(int(b, 16) for b in HEX_BYTE_RE.findall(raw))
        dims = dims_for(name)
        if dims is None:
            print(f"  skip {name}: unknown dims", file=sys.stderr)
            continue
        w, h = dims
        # Adafruit GFX bitmap: row-major, MSB first, padded to byte
        bytes_per_row = (w + 7) // 8
        expected = bytes_per_row * h

        if len(data) != expected:
            # Некоторые иконки имеют лишний ряд всех нулей (артефакт
            # исходного генератора). Если byte_count кратен bytes_per_row
            # и превышает expected — усечём до заявленной высоты.
            if len(data) % bytes_per_row == 0 and len(data) > expected:
                trimmed = len(data) - expected
                data = data[:expected]
                print(
                    f"  note {name}: trimmed {trimmed} padding bytes "
                    f"({trimmed // bytes_per_row} extra row(s))",
                    file=sys.stderr,
                )
            else:
                print(
                    f"  warn {name}: size mismatch (got {len(data)}, "
                    f"expected {expected} for {w}x{h}) — skipped",
                    file=sys.stderr,
                )
                continue
        bitmaps.append(Bitmap(name=name, width=w, height=h, data=data))
    return bitmaps


# ────────────────────────────────────────────────────────────────
# Парсинг маппинга getWeatherIcon96 / getWeatherIcon64
# ────────────────────────────────────────────────────────────────

SWITCH_RE = re.compile(
    r"inline\s+const\s+unsigned\s+char\*\s+getWeatherIcon(\d+)\s*\([^)]*\)\s*\{(.*?)^\}",
    re.MULTILINE | re.DOTALL,
)
CASE_RE = re.compile(
    r"case\s+(\d+):(?:\s*case\s+\d+:)*\s*\n?\s*return\s+night\s*\?\s*(\w+)\s*:\s*(\w+)\s*;",
    re.MULTILINE,
)
# Исходный многострочный паттерн: несколько case подряд, потом return.
# Реализуем через сканирование вручную — надёжнее чем один регекс.
CASE_LINE_RE = re.compile(r"case\s+(\d+)\s*:")
RETURN_LINE_RE = re.compile(
    r"return\s+night\s*\?\s*(\w+)\s*:\s*(\w+)\s*;"
)
FALLBACK_RE = re.compile(
    r'if\s*\(\s*owmIcon\s*==\s*"([^"]+)"\s*\)\s*return\s+(\w+)\s*;'
)
DEFAULT_RE = re.compile(r"return\s+(\w+)\s*;\s*//\s*fallback")


def parse_weather_mapping(source: str) -> dict[str, dict]:
    """Возвращает {'96': {...}, '64': {...}} с полями by_id, by_icon, default."""
    out: dict[str, dict] = {}
    for match in SWITCH_RE.finditer(source):
        size = match.group(1)
        body = match.group(2)

        # Сканируем построчно, накапливая case-и до первого return
        by_id: dict[int, dict[str, str]] = {}
        pending_cases: list[int] = []
        for line in body.splitlines():
            for cm in CASE_LINE_RE.finditer(line):
                pending_cases.append(int(cm.group(1)))
            rm = RETURN_LINE_RE.search(line)
            if rm and pending_cases:
                night_icon, day_icon = rm.group(1), rm.group(2)
                for cid in pending_cases:
                    by_id[cid] = {"day": day_icon, "night": night_icon}
                pending_cases = []

        by_icon: dict[str, str] = {}
        for code, icon in FALLBACK_RE.findall(body):
            by_icon[code] = icon

        default_match = DEFAULT_RE.search(body)
        default_icon = default_match.group(1) if default_match else None

        out[size] = {
            "by_id": by_id,
            "by_icon": by_icon,
            "default": default_icon,
        }
    return out


# ────────────────────────────────────────────────────────────────
# Парсинг layout-констант из display_renderer.cpp и config.h
# ────────────────────────────────────────────────────────────────

CONSTEXPR_RE = re.compile(
    r"static\s+constexpr\s+int\s+(\w+)\s*=\s*([^;]+);"
)
DEFINE_RE = re.compile(r"#define\s+(\w+)\s+(\d+)")


def parse_int_defines(source: str) -> dict[str, int]:
    values: dict[str, int] = {}
    for m in DEFINE_RE.finditer(source):
        try:
            values[m.group(1)] = int(m.group(2))
        except ValueError:
            pass
    return values


def parse_layout(renderer_source: str, config_defines: dict[str, int]) -> dict[str, int]:
    """Вытаскивает layout-константы, подставляя DISPLAY_WIDTH/HEIGHT из config.h."""
    vars_: dict[str, int] = dict(config_defines)
    # min/max и прочее не нужно — все выражения простые
    allowed_globals = {"__builtins__": {}}
    for m in CONSTEXPR_RE.finditer(renderer_source):
        name, expr = m.group(1), m.group(2).strip()
        try:
            value = eval(expr, allowed_globals, vars_)
            # C++ constexpr int использует целочисленную арифметику,
            # Python 3 — float division. Приводим к int, если возможно.
            if isinstance(value, (int, float)):
                vars_[name] = int(value)
        except Exception as exc:
            print(f"  skip {name}: cannot eval {expr!r} ({exc})", file=sys.stderr)

    layout_keys = [
        "W", "H",
        "STATUS_H", "CURRENT_H", "CHART_H", "FORECAST_H",
        "STATUS_Y", "CURRENT_Y", "CHART_Y", "FORECAST_Y",
    ]
    return {k: vars_[k] for k in layout_keys if k in vars_}


# ────────────────────────────────────────────────────────────────
# Извлечение шрифтов Rubik через freetype-py
#
# u8g2-шрифты в rubik_fonts_u8g2.h сгенерированы из Rubik-Variable.ttf.
# Вместо декодирования u8g2 binary-формата (~300 строк кода и риски багов)
# рендерим глифы напрямую из исходного TTF через freetype в монохроме.
# Метрики (advance, bbx) получаются близкими к u8g2-версии, потому что
# рендер идёт из того же файла на ту же пиксельную высоту.
# ────────────────────────────────────────────────────────────────

@dataclass
class Glyph:
    codepoint: int
    width: int
    height: int
    x_off: int    # смещение битмапа от курсора (пикселей)
    y_off: int    # смещение верхней строки битмапа от базовой линии (отрицательное = выше)
    advance: int  # на сколько сдвинуть курсор после отрисовки
    data: bytes   # битмап row-major, MSB first, padded до байта (как в icons)


def pack_ft_bitmap_1bpp(bitmap) -> bytes:
    """freetype с FT_LOAD_TARGET_MONO возвращает 1bpp битмап, где pitch —
    количество байт на строку. Упакуем в row-major MSB-first без паддинга
    до границы 8-пиксельного байта шире ширины (как в Adafruit GFX).
    """
    width = bitmap.width
    rows = bitmap.rows
    pitch = abs(bitmap.pitch)
    src = bytes(bitmap.buffer)

    bytes_per_row = (width + 7) // 8
    out = bytearray(bytes_per_row * rows)

    for y in range(rows):
        # freetype MSB first: первый бит байта — самый левый пиксель
        for x in range(width):
            byte_in = src[y * pitch + (x >> 3)]
            if byte_in & (0x80 >> (x & 7)):
                out[y * bytes_per_row + (x >> 3)] |= 0x80 >> (x & 7)
    return bytes(out)


def extract_font(face, pixel_size: int) -> dict:
    """Рендерит все нужные глифы из TTF при заданной пиксельной высоте."""
    import freetype as ft

    # set_pixel_sizes(width=0, height=N) → N-пиксельная высота
    face.set_pixel_sizes(0, pixel_size)

    # freetype-py: face.size это SizeMetrics напрямую (26.6 fixed-point)
    metrics = face.size
    ascender = metrics.ascender >> 6      # 26.6 fixed → пиксели
    descender = metrics.descender >> 6    # отрицательное
    line_height = metrics.height >> 6

    glyphs: dict[str, dict] = {}

    for cp in FONT_CODEPOINTS:
        face.load_char(
            chr(cp),
            ft.FT_LOAD_RENDER | ft.FT_LOAD_TARGET_MONO | ft.FT_LOAD_NO_HINTING,
        )
        slot = face.glyph
        bitmap = slot.bitmap
        advance_px = slot.advance.x >> 6

        if bitmap.width == 0 or bitmap.rows == 0:
            # пробел и прочие пустые — только advance
            glyphs[str(cp)] = {
                "w": 0,
                "h": 0,
                "x_off": 0,
                "y_off": 0,
                "advance": advance_px,
                "b64": "",
            }
            continue

        data = pack_ft_bitmap_1bpp(bitmap)
        # bitmap_top — расстояние от базовой линии до ВЕРХА битмапа
        # (положительное значит битмап выше базовой линии).
        # В нашем рендере y_off будет добавляться к Y-координате рисования,
        # и мы ожидаем, что drawUTF8(x, y) рисует базовую линию в y.
        # Тогда верх битмапа глифа = y - bitmap_top.
        glyphs[str(cp)] = {
            "w": bitmap.width,
            "h": bitmap.rows,
            "x_off": slot.bitmap_left,
            "y_off": -slot.bitmap_top,  # отрицательное = выше базовой линии
            "advance": advance_px,
            "b64": base64.b64encode(data).decode("ascii"),
        }

    return {
        "pixel_size": pixel_size,
        "ascent": ascender,
        "descent": descender,
        "line_height": line_height,
        "glyphs": glyphs,
    }


def extract_fonts() -> dict:
    import freetype as ft

    if not TTF_PATH.exists():
        print(f"  warn: {TTF_PATH} not found — fonts will be empty", file=sys.stderr)
        return {}

    face = ft.Face(str(TTF_PATH))
    fonts: dict[str, dict] = {}
    for size in FONT_SIZES:
        name = f"rubik_{size}"
        fonts[name] = extract_font(face, size)
        count = len(fonts[name]["glyphs"])
        print(f"  rubik_{size}: {count} glyph(s)")
    return fonts


# ────────────────────────────────────────────────────────────────
# Главный пайплайн
# ────────────────────────────────────────────────────────────────

def write_json(path: Path, data) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"  -> {path.relative_to(ROOT)}")


def main() -> int:
    print("Извлечение ассетов из прошивки...")

    # --- Иконки (icons.h) ---
    icons_src = ICONS_H.read_text(encoding="utf-8")
    icons = parse_bitmaps(icons_src)
    icons_json = {bm.name: bm.to_json() for bm in icons}
    print(f"icons.h: {len(icons)} bitmap(s)")
    write_json(ASSETS_DIR / "icons.json", icons_json)

    # --- Погодные иконки (weather_icons.h) ---
    weather_src = WEATHER_ICONS_H.read_text(encoding="utf-8")
    weather_bitmaps = parse_bitmaps(weather_src)
    mapping = parse_weather_mapping(weather_src)
    weather_json = {
        "bitmaps": {bm.name: bm.to_json() for bm in weather_bitmaps},
        "mapping": mapping,
    }
    print(
        f"weather_icons.h: {len(weather_bitmaps)} bitmap(s), "
        f"mapping sizes: {list(mapping.keys())}"
    )
    write_json(ASSETS_DIR / "weather_icons.json", weather_json)

    # --- Layout-константы ---
    renderer_src = RENDERER_CPP.read_text(encoding="utf-8")
    config_src = CONFIG_H.read_text(encoding="utf-8")
    config_defines = parse_int_defines(config_src)
    layout = parse_layout(renderer_src, config_defines)
    print(f"layout: {layout}")
    write_json(ASSETS_DIR / "layout.json", layout)

    # --- Шрифты Rubik из TTF ---
    print("fonts:")
    fonts = extract_fonts()
    if fonts:
        write_json(ASSETS_DIR / "fonts.json", fonts)

    print("Готово.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
