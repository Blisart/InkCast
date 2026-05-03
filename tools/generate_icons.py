"""
Скачивает Weather Underground иконки (solid-black, 128x128 PNG)
и конвертирует в C header с bitmap-массивами для GxEPD2.
Генерирует два размера: 96x96 (текущая погода) и 64x64 (прогноз).
Маппинг: сначала по OWM weather.id (точный), fallback по icon code.
"""

import urllib.request
import os
from PIL import Image
import io

# ═══════════════════════════════════════════
# Все WU иконки для скачивания
# ═══════════════════════════════════════════

WU_ICONS = sorted([
    # Дневные
    "chanceflurries", "chancerain", "chancesleet", "chancesnow", "chancetstorms",
    "cloudy", "flurries", "fog", "hazy", "mostlycloudy", "mostlysunny",
    "rain", "sleet", "snow", "sunny", "tstorms",
    # Ночные
    "nt_chanceflurries", "nt_chancerain", "nt_chancesleet", "nt_chancesnow",
    "nt_chancetstorms", "nt_clear", "nt_cloudy", "nt_flurries", "nt_fog",
    "nt_hazy", "nt_mostlycloudy", "nt_partlycloudy",
    "nt_rain", "nt_sleet", "nt_snow", "nt_tstorms",
])

# ═══════════════════════════════════════════
# Маппинг OWM icon code → WU icon (fallback)
# ═══════════════════════════════════════════
# Шкала облачности (день): sunny → mostlysunny → mostlycloudy → cloudy
#   mostlysunny / partlycloudy  = большое солнце + маленькое облако
#   mostlycloudy / partlysunny  = большое облако + маленькое солнце
#   cloudy                      = только облака, без солнца

OWM_ICON_TO_WU = {
    "01d": "sunny",              # ясно
    "01n": "nt_clear",           # ясно (ночь)
    "02d": "mostlysunny",       # мало облаков → большое солнце + маленькое облако
    "02n": "nt_partlycloudy",   # мало облаков (ночь)
    "03d": "mostlycloudy",      # рассеянные облака → большое облако + маленькое солнце
    "03n": "nt_mostlycloudy",   # рассеянные облака (ночь)
    "04d": "cloudy",            # пасмурно → только облака
    "04n": "nt_cloudy",         # пасмурно (ночь)
    "09d": "chancerain",
    "09n": "nt_chancerain",
    "10d": "rain",
    "10n": "nt_rain",
    "11d": "tstorms",
    "11n": "nt_tstorms",
    "13d": "snow",
    "13n": "nt_snow",
    "50d": "fog",
    "50n": "nt_fog",
}

# ═══════════════════════════════════════════
# Маппинг OWM weather.id → WU icon (точный)
# Только для id, где нужна иконка отличная от icon-based маппинга.
# Формат: id → (day_icon, night_icon)
# ═══════════════════════════════════════════

OWM_ID_TO_WU = {
    # Гроза лёгкая / с моросью
    200: ("chancetstorms", "nt_chancetstorms"),  # гроза с лёгким дождём
    210: ("chancetstorms", "nt_chancetstorms"),  # лёгкая гроза
    230: ("chancetstorms", "nt_chancetstorms"),  # гроза с лёгкой моросью
    231: ("chancetstorms", "nt_chancetstorms"),  # гроза с моросью
    # Морось сильная (3xx, icon 09d → chancerain по fallback, но сильные → rain)
    302: ("rain", "nt_rain"),                     # сильная морось
    312: ("rain", "nt_rain"),                     # сильный моросящий дождь
    313: ("rain", "nt_rain"),                     # ливень с моросью
    314: ("rain", "nt_rain"),                     # сильный ливень с моросью
    # Дождь лёгкий (500, icon 10d → rain по fallback, но лёгкий → chancerain)
    500: ("chancerain", "nt_chancerain"),         # лёгкий дождь
    # Ледяной дождь
    511: ("chancesleet", "nt_chancesleet"),       # замерзающий дождь
    # Ливни сильные (icon 09d → chancerain по fallback, но сильные → rain)
    521: ("rain", "nt_rain"),                     # ливень
    522: ("rain", "nt_rain"),                     # сильный ливень
    531: ("rain", "nt_rain"),                     # порывистый ливень
    # Снег лёгкий
    600: ("chancesnow", "nt_chancesnow"),         # лёгкий снег
    # Мокрый снег / sleet
    611: ("sleet", "nt_sleet"),                   # мокрый снег
    612: ("chancesleet", "nt_chancesleet"),        # лёгкий мокрый снег
    613: ("sleet", "nt_sleet"),                   # ливневый мокрый снег
    615: ("chancesleet", "nt_chancesleet"),        # лёгкий дождь со снегом
    616: ("sleet", "nt_sleet"),                   # дождь со снегом
    # Метель / flurries
    620: ("chanceflurries", "nt_chanceflurries"), # лёгкая метель
    621: ("flurries", "nt_flurries"),             # метель
    # Облачность — 803 отличается от 804 (оба дают icon 04d)
    803: ("mostlycloudy", "nt_mostlycloudy"),     # облачно с прояснениями (51-84%)
    # Атмосфера — дымка/пыль/смог (не туман)
    711: ("hazy", "nt_hazy"),                     # дым
    721: ("hazy", "nt_hazy"),                     # дымка
    731: ("hazy", "nt_hazy"),                     # песчаные вихри
    751: ("hazy", "nt_hazy"),                     # песок
    761: ("hazy", "nt_hazy"),                     # пыль
    762: ("hazy", "nt_hazy"),                     # вулканический пепел
    # Атмосфера — шквал/торнадо (icon 50d → fog по fallback, но это не туман)
    771: ("tstorms", "nt_tstorms"),               # шквал
    781: ("tstorms", "nt_tstorms"),               # торнадо
}

# Источник — 128x128 PNG (масштабируем вниз для лучшего качества)
BASE_URL = "https://raw.githubusercontent.com/manifestinteractive/weather-underground-icons/master/dist/icons/solid-black/png/128x128"
SIZES = [96, 64]  # размеры для генерации
OUTPUT_DIR = os.path.join(os.path.dirname(__file__), "..", "inkcast")


def download_png(icon_name):
    """Скачивает PNG иконку, возвращает bytes"""
    url = f"{BASE_URL}/{icon_name}.png"
    print(f"  Скачиваю: {url}")
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=15) as resp:
            return resp.read()
    except Exception as e:
        print(f"  ОШИБКА: {e}")
        return None


def png_to_bitmap(png_data, size):
    """Конвертирует PNG в монохромный bitmap-массив (MSB first, как ожидает GxEPD2)"""
    img = Image.open(io.BytesIO(png_data))
    img = img.convert("RGBA")
    img = img.resize((size, size), Image.LANCZOS)

    # Белый фон для прозрачных пикселей
    bg = Image.new("RGBA", (size, size), (255, 255, 255, 255))
    bg.paste(img, mask=img.split()[3])
    img = bg.convert("L")  # Grayscale

    # Порог: < 128 → чёрный (бит 1), >= 128 → белый (бит 0)
    pixels = list(img.getdata())
    bitmap = []
    for i in range(0, len(pixels), 8):
        byte = 0
        for bit in range(8):
            if i + bit < len(pixels):
                if pixels[i + bit] < 128:
                    byte |= (0x80 >> bit)
        bitmap.append(byte)
    return bitmap


def bitmap_to_c_array(name, bitmap, size):
    """Генерирует C-массив из bitmap данных"""
    lines = [f"// {size}x{size} Weather Underground icon"]
    lines.append(f"const unsigned char icon{size}_{name}[] PROGMEM = {{")
    for i in range(0, len(bitmap), 16):
        chunk = bitmap[i:i+16]
        hex_str = ", ".join(f"0x{b:02X}" for b in chunk)
        lines.append(f"    {hex_str},")
    lines.append("};")
    return "\n".join(lines)


def generate_id_switch(size, arrays):
    """Генерирует switch/case по weather.id для C++"""
    # Группируем id по паре (day, night) иконок
    groups = {}
    for wid, (day, night) in sorted(OWM_ID_TO_WU.items()):
        key = (day, night)
        groups.setdefault(key, []).append(wid)

    lines = []
    lines.append("    switch (weatherId) {")
    for (day, night), ids in sorted(groups.items(), key=lambda x: x[1][0]):
        day_c = day.replace("-", "_")
        night_c = night.replace("-", "_")
        # Проверяем что иконки есть в наборе
        if day not in arrays[size] or night not in arrays[size]:
            continue
        case_str = " ".join(f"case {i}:" for i in ids)
        lines.append(f"        {case_str}")
        lines.append(f"            return night ? icon{size}_{night_c} : icon{size}_{day_c};")
    lines.append("    }")
    return lines


def generate_header():
    print(f"Всего иконок для скачивания: {len(WU_ICONS)}")

    # Скачиваем PNG
    png_cache = {}
    for wu_name in WU_ICONS:
        print(f"\n[{wu_name}]")
        png_data = download_png(wu_name)
        if png_data is None:
            print(f"  Пропускаю {wu_name}")
            continue
        png_cache[wu_name] = png_data
        print(f"  OK")

    # Генерируем bitmap-массивы для каждого размера
    arrays = {}  # {size: {wu_name: c_array_string}}
    for size in SIZES:
        arrays[size] = {}
        for wu_name, png_data in sorted(png_cache.items()):
            bitmap = png_to_bitmap(png_data, size)
            c_name = wu_name.replace("-", "_")
            arrays[size][wu_name] = bitmap_to_c_array(c_name, bitmap, size)
            print(f"  {wu_name} → {size}x{size} ({len(bitmap)} bytes)")

    # Генерируем header
    h_lines = [
        "#pragma once",
        "",
        "#include <Arduino.h>",
        "",
        "// Weather Underground icons (solid-black)",
        "// Автоматически сгенерировано из github.com/manifestinteractive/weather-underground-icons",
        f"// Размеры: {', '.join(str(s) + 'x' + str(s) for s in SIZES)}",
        f"// Иконок: {len(png_cache)} ({len(WU_ICONS)} запрошено)",
        "",
    ]

    # Массивы для каждого размера
    for size in SIZES:
        h_lines.append(f"// ═══════════════ {size}x{size} ═══════════════")
        h_lines.append("")
        for wu_name in sorted(arrays[size].keys()):
            h_lines.append(arrays[size][wu_name])
            h_lines.append("")

    # Функции маппинга: getWeatherIcon{size}(int weatherId, const String& owmIcon)
    for size in SIZES:
        h_lines.append(f"// Маппинг OWM → bitmap {size}x{size}")
        h_lines.append(f"// Приоритет: weather.id (точный) → icon code (fallback)")
        h_lines.append(f"inline const unsigned char* getWeatherIcon{size}(int weatherId, const String& owmIcon) {{")
        h_lines.append(f"    bool night = owmIcon.endsWith(\"n\");")
        h_lines.append("")

        # switch по weather.id
        h_lines.extend(generate_id_switch(size, arrays))
        h_lines.append("")

        # fallback по icon code
        h_lines.append("    // Fallback по OWM icon code")
        for owm_code, wu_name in sorted(OWM_ICON_TO_WU.items()):
            c_name = wu_name.replace("-", "_")
            if wu_name in arrays[size]:
                h_lines.append(f'    if (owmIcon == "{owm_code}") return icon{size}_{c_name};')
        h_lines.append(f"    return icon{size}_cloudy; // fallback")
        h_lines.append("}")
        h_lines.append("")

    output_path = os.path.join(OUTPUT_DIR, "weather_icons.h")
    with open(output_path, "w", encoding="utf-8") as f:
        f.write("\n".join(h_lines))
    print(f"\nГотово! Записано: {output_path}")
    print(f"Иконок: {len(png_cache)}, размеров: {len(SIZES)}")


if __name__ == "__main__":
    generate_header()
