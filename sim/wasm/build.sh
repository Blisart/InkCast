#!/bin/bash
# ================================================================
# build.sh — сборка WASM-симулятора e-paper дисплея через Emscripten.
#
# Компилирует реальный display_renderer.cpp прошивки в WebAssembly.
# Мок-заголовки (sim/wasm/mocks/) подменяют Arduino/GxEPD2/SPI.
#
# Зависимости:
#   - Emscripten SDK (emsdk) установлен и активирован (emcc в PATH)
#   - Библиотека U8g2_for_Adafruit_GFX в Arduino libraries
#
# Использование:
#   cd sim/wasm && bash build.sh
#
# Результат:
#   sim/wasm/renderer.js + renderer.wasm
# ================================================================

set -e

PROJ_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
FW="$PROJ_ROOT/inkcast"
MOCKS="$PROJ_ROOT/sim/wasm/mocks"
WASM_DIR="$PROJ_ROOT/sim/wasm"

# Путь к библиотеке U8g2_for_Adafruit_GFX.
# Сначала проверяем переменную окружения, затем стандартные пути Arduino.
if [ -n "$U8G2_LIB" ]; then
    U8G2_SRC="$U8G2_LIB"
elif [ -d "$HOME/Documents/Arduino/libraries/U8g2_for_Adafruit_GFX/src" ]; then
    U8G2_SRC="$HOME/Documents/Arduino/libraries/U8g2_for_Adafruit_GFX/src"
elif [ -d "$HOME/Arduino/libraries/U8g2_for_Adafruit_GFX/src" ]; then
    U8G2_SRC="$HOME/Arduino/libraries/U8g2_for_Adafruit_GFX/src"
else
    echo "ERROR: U8g2_for_Adafruit_GFX library not found."
    echo "Set U8G2_LIB environment variable to the src/ directory."
    exit 1
fi

echo "Project root: $PROJ_ROOT"
echo "Firmware:     $FW"
echo "Mocks:        $MOCKS"
echo "U8g2 library: $U8G2_SRC"
echo ""

# Исходники для компиляции
SOURCES=(
    "$WASM_DIR/entry.cpp"
    "$FW/display_renderer.cpp"
    "$U8G2_SRC/U8g2_for_Adafruit_GFX.cpp"
    "$WASM_DIR/u8g2_font_7x14.cpp"
)

# Порядок include-путей: mocks первым, чтобы перехватить Arduino.h и т.д.
INCLUDES=(
    -I "$MOCKS"
    -I "$FW"
    -I "$U8G2_SRC"
)

# Экспортируемые функции (с префиксом _)
EXPORTS="[
    '_init',
    '_get_framebuffer',
    '_get_framebuffer_size',
    '_get_display_width',
    '_get_display_height',
    '_set_current',
    '_set_forecast',
    '_set_chart',
    '_set_meta',
    '_set_render_params',
    '_render_normal',
    '_render_error',
    '_malloc',
    '_free'
]"
# Убираем пробелы и переносы строк для emcc
EXPORTS=$(echo "$EXPORTS" | tr -d ' \n')

# Найти emcc: сначала в PATH, потом в стандартном месте emsdk
EMCC=$(command -v emcc 2>/dev/null || echo "")
if [ -z "$EMCC" ] && [ -f "C:/tools/emsdk/upstream/emscripten/emcc.bat" ]; then
    EMCC="C:/tools/emsdk/upstream/emscripten/emcc.bat"
fi
if [ -z "$EMCC" ]; then
    echo "ERROR: emcc not found. Install emsdk or add emcc to PATH."
    exit 1
fi
echo "Using: $EMCC"

echo "Building WASM renderer..."
"$EMCC" \
    "${SOURCES[@]}" \
    "${INCLUDES[@]}" \
    -std=c++17 \
    -O2 \
    -s WASM=1 \
    -s MODULARIZE=1 \
    -s EXPORT_NAME="createRenderer" \
    -s EXPORTED_FUNCTIONS="$EXPORTS" \
    -s EXPORTED_RUNTIME_METHODS="['ccall','cwrap','stringToUTF8','UTF8ToString','HEAPU8']" \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s INITIAL_MEMORY=4194304 \
    -s NO_EXIT_RUNTIME=1 \
    -o "$WASM_DIR/renderer.js"

echo ""
echo "Done! Output:"
echo "  $WASM_DIR/renderer.js"
echo "  $WASM_DIR/renderer.wasm"
echo ""
echo "Open sim/wasm/index.html via a local HTTP server, e.g.:"
echo "  npx serve $WASM_DIR"
