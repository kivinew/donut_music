import sys
import time
import os
import platform
import pygame
import base64
from io import BytesIO
from music import LOOP_WAV_B64
from palettes import PALETTES, PALETTE_NAMES, interpolate_palette

if platform.system() == "Windows":
    import ctypes
    try:
        import donut_c.donut_renderer as donut_renderer
    except ImportError:
        print("ОШИБКА: C расширение не скомпилировано!")
        print("Сначала выполните команду: uv run setup.py build_ext --inplace")
        print("Или запустите RUN_DONUT_CONSOLE.bat")
        input("Нажмите Enter для выхода...")
        sys.exit(1)

    kernel32 = ctypes.windll.kernel32
    user32 = ctypes.windll.user32

"""
Сборка исполняемого файла EXE:
uv run -m pyinstaller --onefile --console main.py (или сначала uv run setup.py build_ext --inplace)
"""

def read_key():
    if user32.GetAsyncKeyState(ord('Q')) & 0x8000 or \
       user32.GetAsyncKeyState(0x1B) & 0x8000:
        return 'quit'
    if user32.GetAsyncKeyState(0x26) & 0x8000 or \
       user32.GetAsyncKeyState(ord('W')) & 0x8000 or \
       user32.GetAsyncKeyState(0x6B) & 0x8000 or \
       user32.GetAsyncKeyState(0xBB) & 0x8000:
        return 'plus'
    if user32.GetAsyncKeyState(0x28) & 0x8000 or \
       user32.GetAsyncKeyState(ord('S')) & 0x8000 or \
       user32.GetAsyncKeyState(0x6D) & 0x8000 or \
       user32.GetAsyncKeyState(0xBD) & 0x8000:
        return 'minus'
    if user32.GetAsyncKeyState(ord('0')) & 0x8000:
        return 'reset'
    if user32.GetAsyncKeyState(0x25) & 0x8000:
        return 'palette_prev'
    if user32.GetAsyncKeyState(0x27) & 0x8000:
        return 'palette_next'
    if user32.GetAsyncKeyState(0x20) & 0x8000:
        return 'pause'
    if user32.GetAsyncKeyState(ord('M')) & 0x8000:
        return 'music'
    return None

def init_music():
    try:
        pygame.mixer.init()
        sound_data = base64.b64decode(LOOP_WAV_B64)
        pygame.mixer.music.load(BytesIO(sound_data))
        pygame.mixer.music.play(-1)
    except:
        pass

if __name__ == "__main__":
    A = B = 0.0
    light_time = 0.0
    zoom = 0.3
    paused = False
    last_pause_time = 0
    last_input_time = 0
    running = True
    volume_levels = [1.0, 0.75, 0.5, 0.25, 0.0]
    current_volume_idx = 0
    volume_direction_down = True
    current_palette_idx = 1
    auto_palette_cycle = True
    manual_palette_time = 0.0
    frame_count = 0
    last_time = time.time()
    target_fps = 60
    frame_time = 1.0 / target_fps

    init_music()
    donut_renderer.init_console()
    donut_renderer.set_palette(PALETTES[current_palette_idx])

    try:
        while running:
            start_frame = time.time()

            key = read_key()
            current_time = time.time()

            if key == 'quit':
                running = False
            elif key == 'plus':
                if current_time - last_input_time > 0.15:
                    zoom = min(zoom * 1.2, 2.5)
                    last_input_time = current_time
            elif key == 'minus':
                if current_time - last_input_time > 0.15:
                    zoom = max(zoom / 1.2, 0.3)
                    last_input_time = current_time
            elif key == 'palette_prev':
                if current_time - last_input_time > 0.25:
                    current_palette_idx = (current_palette_idx - 1) % len(PALETTE_NAMES)
                    donut_renderer.set_palette(PALETTES[current_palette_idx])
                    last_input_time = current_time
                    manual_palette_time = current_time + 8.0
            elif key == 'palette_next':
                if current_time - last_input_time > 0.25:
                    current_palette_idx = (current_palette_idx + 1) % len(PALETTE_NAMES)
                    donut_renderer.set_palette(PALETTES[current_palette_idx])
                    last_input_time = current_time
                    manual_palette_time = current_time + 8.0
            elif key == 'reset':
                if current_time - last_input_time > 0.3:
                    zoom = 1.0
                    A = 0.0
                    B = 0.0
                    last_input_time = current_time
            elif key == 'pause':
                if current_time - last_pause_time > 0.3:
                    paused = not paused
                    last_pause_time = current_time
            elif key == 'music':
                if current_time - last_input_time > 0.3:
                    if volume_direction_down:
                        current_volume_idx += 1
                        if current_volume_idx >= len(volume_levels) - 1:
                            volume_direction_down = False
                    else:
                        current_volume_idx -= 1
                        if current_volume_idx <= 0:
                            volume_direction_down = True
                    pygame.mixer.music.set_volume(volume_levels[current_volume_idx])
                    last_input_time = current_time

            donut_renderer.render_frame(A, B, light_time, zoom)

            if not paused:
                A += 0.04
                B += 0.02
            light_time += 0.05

            if auto_palette_cycle and current_time > manual_palette_time:
                cycle_time = current_time / 18.0
                total_palettes = len(PALETTES)
                position = (cycle_time % total_palettes)
                idx1 = int(position) % total_palettes
                idx2 = (idx1 + 1) % total_palettes
                t = position - int(position)

                interpolated = interpolate_palette(PALETTES[idx1], PALETTES[idx2], t)
                donut_renderer.set_palette(interpolated)

                if t > 0.5:
                    current_palette_idx = idx2
                else:
                    current_palette_idx = idx1

            frame_count += 1
            current_time = time.time()
            if current_time - last_time >= 1.0:
                fps = frame_count / (current_time - last_time)
                kernel32.SetConsoleTitleW(
                    f"Donut Music 🍩  FPS: {fps:.1f} | Палитра ←/→ {PALETTE_NAMES[current_palette_idx]} | Зум ↑/↓: {zoom*100:.0f}% | M Громкость: {volume_levels[current_volume_idx]*100:.0f}% | Space Вращение | Q/Esc Выход"
                )
                frame_count = 0
                last_time = current_time

            elapsed = time.time() - start_frame
            if elapsed < frame_time:
                time.sleep(frame_time - elapsed)

    except KeyboardInterrupt:
        pass
    finally:
        donut_renderer.cleanup_console()
        print("\nDonut stopped.")