# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

Donut Music is a Python console application that renders an animated 3D ASCII donut with music playback. The active runtime path is Windows-focused: `main.py` imports the compiled `donut_c.donut_renderer` extension and uses Win32 console APIs for rendering, cursor hiding, console buffer switching, and console-title FPS display.

There are no Cursor or Copilot rule files in the repository.

## Commands

Use `uv` for dependency management.

```bash
uv sync
```

Build the native renderer extension before running the app from source:

```bash
uv run setup.py build_ext --inplace
```

Run the app:

```bash
uv run main.py
```

Windows quick path used by the repository:

```powershell
.\RUN_DONUT_CONSOLE.bat
```

`RUN_DONUT_CONSOLE.bat` creates/activates `.venv`, installs dependencies from `requirements.txt`, builds the C extension, builds a PyInstaller executable, and then launches `dist\main.exe`.

Build a console executable manually:

```bash
uv run -m pyinstaller --onefile --console main.py
```

There is currently no configured test suite. The GitHub workflow installs `flake8` and `pytest` separately and runs:

```bash
flake8 . --count --select=E9,F63,F7,F82 --show-source --statistics
flake8 . --count --exit-zero --max-complexity=10 --max-line-length=127 --statistics
pytest
```

## Architecture

- `main.py` is the application entry point. It owns the 60 FPS loop, keyboard polling through `GetAsyncKeyState`, zoom/palette/shape/music controls, and palette interpolation state. Shape switching is exposed as `N` for next and `X` for previous.
- `donut_c/donut_renderer.c` is the performance-sensitive renderer exposed as a Python C extension. It creates a Win32 console screen buffer, maintains `screenBuffer` and `zbuffer`, renders the donut, applies lighting, draws extra 3D figures, and writes frames with `WriteConsoleOutput`.
- `setup.py` builds the extension as `donut_c.donut_renderer` from `donut_c/donut_renderer.c`. It is Windows/MSVC-oriented: compile args use `/O2`, `/Oi`, `/Ot`, `/Oy`, `/GL`, `/arch:SSE2`, `/fp:fast`, `/W3`, and links `user32` and `kernel32`.
- `palettes.py` defines packed console color attributes. Each palette entry is a `WORD` where the high nibble is background color and the low nibble is foreground color; `interpolate_palette()` blends adjacent palette entries for smooth transitions.
- `girl.py` owns the standalone dancing-girl wireframe model. The C renderer imports its `VERTICES` and `EDGES` through `set_girl_model()` and reuses the same vertex/edge deformation logic for the in-app girl shape.
- `music.py` stores the loop audio as an embedded Base64 WAV string. It is very large, so avoid reading or editing it unless the task is specifically about the audio asset.
- `main_old_version.py` and `music to text.py` are obsolete helpers and are not part of the current runtime.
- `pyproject.toml` currently pins `pygame-ce`, `pyinstaller`, `colorama`, and build-related packages through `uv`. The workflow still uses `requirements.txt` plus separate `flake8`/`pytest` installs, so local CI parity requires installing those tools separately.

## Important local constraints

- The compiled extension is ignored by `.gitignore` (`donut_c/*.pyd`), but generated `build/`, `dist/`, `*.spec`, and egg-info artifacts are present in the working tree snapshot and should not be treated as source unless the task is packaging-related.
- The renderer depends on Windows console behavior. Non-Windows compatibility is not implemented in the active `main.py` path.
- If changing palette behavior, remember that renderer-side `set_palette()` copies only up to `MAX_PALETTE_SIZE` entries and then renders luminance buckets modulo `palette_size`.
