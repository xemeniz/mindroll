# MINDROLL · MIND SERIES · Dj Nané

Vocal stutter build: coge un trozo de vocal y lo repite cada vez más rápido (1/4 → 1/8 → 1/16 → 1/32) y cada vez más largo hasta el drop. Va en la pista de la vocal.

- JUCE 8 + CMake. Interfaz web embebida (`ui/index.html`, generado con `python3 tools/build_ui.py` desde `ui/src.html` + `common/`).
- `common/`: piezas de la skin v2 de la MIND SERIES (CSS común, fuentes Nunito, grano, barra nativa de arrastre). Es el comienzo de la base común de la serie.
- Motor: `Source/RollEngine.*` (render offline del build). Pruebas: `cmake -B build -DMO_TESTS=ON && cmake --build build --target MoEngineTest` y `node tests/ui.test.mjs`.
- El build de Mac sale en GitHub Actions como Release (VST3, AU y Standalone).
