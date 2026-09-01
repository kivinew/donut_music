# AGENTS.md — Donut Music

Animated 3D ASCII donut with music visualization in terminal.

## Project Structure
```
donut_music/
├── main.py                # Entry point: UI loop, 3D rendering dispatch, keyboard handling
├── donut_c/
│   └── donut_renderer.c   # Original C implementation (reference)
├── palettes.py            # Color palettes for rendering
├── music.py               # Pygame-based audio playback
├── shapes/                # 3D shape definitions (donut, cube, heart, girl, galaxy, solar system)
├── config/                # Shape-specific parameters (sizes, animation frames)
└── data/                  # Assets (fonts, music tracks)
```

## Quick Start
```bash
uv sync          # install dependencies (colorama, pygame-ce)
uv run main.py   # launch interactive 3D animation
```

## Controls
| Key | Action |
|-----|--------|
| ←/→ | Switch color palette |
| +/-/↑/W | Zoom in |
| -/↓/S | Zoom out |
| 0 | Reset zoom |
| N/X | Cycle 3D shapes forward/backward |
| M | Toggle music |
| Q/Esc | Quit |

## Environment
- Python 3.12+
- Dependencies: `colorama` (cross-platform terminal colors), `pygame-ce` (audio), `pyinstaller` (optional, for `.exe` builds)

## Notes for Agents
- Flat project structure — all Python modules are at the repo root.
- The C file in `donut_c/` is reference only; the Python port in `main.py` is the active implementation.
- Shape definitions live in `shapes/` with per-shape config files in `config/`.
- No test suite; testing is visual/interactive via running `main.py`.
- PyInstaller build: `pyinstaller main.py` produces a standalone executable.