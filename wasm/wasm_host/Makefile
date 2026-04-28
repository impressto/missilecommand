# ─────────────────────────────────────────────────────────────────────────────
# Missile Command — WebAssembly build
#
# Prerequisites:
#   source /path/to/emsdk/emsdk_env.sh   (Emscripten SDK must be activated)
#
# Targets:
#   make          — build release WASM
#   make debug    — build with -g, AssertionError checks, source maps
#   make serve    — build + start a local HTTP server on port 8080
#   make clean    — remove build artefacts
# ─────────────────────────────────────────────────────────────────────────────

CC      := emcc
OUTDIR  := build
TARGET  := $(OUTDIR)/missile_command.js

SRCS    := ../game/missile_command.c \
           hal_wasm.c \
           main_wasm.c \
           lodepng.cpp \
           lodepng_wrapper.cpp

CFLAGS_COMMON := \
    -I../game \
    -I. \
    -include config.h \
    -Wall -Wextra \
    -O2 \
    -DTARGET_WASM=1

# Emscripten linker flags
# -sEXPORTED_FUNCTIONS: C functions callable from JS (prefixed with _)
# -sEXPORTED_RUNTIME_METHODS: JS helpers we use (wasmMemory for framebuffer blit)
# -sALLOW_MEMORY_GROWTH: allows WASM heap to grow (not needed on ESP32)
LDFLAGS := \
    -sEXPORTED_FUNCTIONS='["_main","_hal_wasm_set_cursor","_hal_wasm_set_fire","_hal_wasm_tap_at"]' \
    -sEXPORTED_RUNTIME_METHODS='["wasmMemory"]' \
    -sALLOW_MEMORY_GROWTH=1 \
    -sSINGLE_FILE=0 \
    --shell-file shell.html \
    --preload-file ../images/background-1.png@background-1.png \
    --preload-file ../images/civilian-target.png@civilian-target.png \
    --preload-file ../images/bunker-1.png@bunker-1.png \
    --preload-file ../sounds/alert.mp3@alert.mp3 \
    --preload-file ../sounds/explode.mp3@explode.mp3 \
    --preload-file ../sounds/explosions.mp3@explosions.mp3 \
    --preload-file ../sounds/finale.mp3@finale.mp3 \
    --preload-file ../sounds/missile-2.mp3@missile-2.mp3 \
    --preload-file ../sounds/roll-up.mp3@roll-up.mp3 \
    --preload-file ../sounds/swoop-up.mp3@swoop-up.mp3 \
    -o $(OUTDIR)/missile_command.html

# ── Default (release) ────────────────────────────────────────────────────────
.PHONY: all
all: $(OUTDIR) $(TARGET)

$(OUTDIR):
	mkdir -p $(OUTDIR)

IMAGES  := ../images/background-1.png ../images/civilian-target.png ../images/bunker-1.png
SOUNDS  := ../sounds/alert.mp3 ../sounds/explode.mp3 ../sounds/explosions.mp3 \
           ../sounds/finale.mp3 ../sounds/missile-2.mp3 \
           ../sounds/roll-up.mp3 ../sounds/swoop-up.mp3

$(TARGET): $(SRCS) shell.html ../game/hal.h ../game/missile_command.h config.h $(IMAGES) $(SOUNDS)
	$(CC) $(CFLAGS_COMMON) $(LDFLAGS) $(SRCS)
	@echo ""
	@echo "  Build complete → open $(OUTDIR)/missile_command.html"
	@echo "  Run 'make serve' to start a local server."

# ── Debug build ──────────────────────────────────────────────────────────────
.PHONY: debug
debug: CFLAGS_COMMON += -g -O0 -sASSERTIONS=2 -sSAFE_HEAP=1
debug: $(OUTDIR) $(TARGET)

# ── Local dev server (Python 3) ──────────────────────────────────────────────
.PHONY: serve
serve: all
	@echo "Serving on http://localhost:8080/missile_command.html"
	cd $(OUTDIR) && python3 -m http.server 8080

# ── Clean ────────────────────────────────────────────────────────────────────
.PHONY: clean
clean:
	rm -rf $(OUTDIR)
