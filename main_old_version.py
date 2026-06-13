#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import sys
import math
import time
import base64
from io import BytesIO
from colorama import init, Style
import os
import ctypes
import subprocess
import shutil
import platform
import msvcrt
import select
from music import LOOP_WAV_B64

"""
Сборка исполняемого файла EXE:
uv run -m pyinstaller --onefile --console main.py (или сначала uv run setup.py build_ext --inplace)
"""
# ------------------------------------------------------------
# Консоль и её размер
# ------------------------------------------------------------
def set_console_title(title: str) -> None:
    """Устанавливает заголовок окна консоли (Windows + ANSI)."""
    if platform.system() == "Windows":
        ctypes.windll.kernel32.SetConsoleTitleW(title)
    else:
        print(f"\033]0;{title}\a", end="", flush=True)


def lock_console_size_and_setup_geometry(target_cols: int = 100,
                                        target_lines: int = 30) -> None:
    """
    1) Принудительно задаёт размер окна/буфера (Windows – через `mode con`,
       другие ОС – берёт текущий размер).
    2) Сохраняет полученные WIDTH, HEIGHT и вычисляет K1.
    3) Отключает возможность ресайза и максимизации окна
       (только Windows).
    """
    global WIDTH, HEIGHT, K1

    if os.name != "nt":
        size = shutil.get_terminal_size(fallback=(80, 24))
        WIDTH, HEIGHT = size.columns, size.lines
        WIDTH = max(40, WIDTH - 2)
        HEIGHT = max(20, HEIGHT - 2)
        K1 = WIDTH * K2 * 3 / (8 * (R1 + R2))
        return

    # 1) Windows – задаём размеры через `mode con`
    subprocess.run(
        ["cmd", "/c", f"mode con: cols={target_cols} lines={target_lines}"],
        shell=False,
        check=False,
    )

    # 2) Считываем реальные размеры окна
    size = shutil.get_terminal_size(fallback=(target_cols, target_lines))
    WIDTH, HEIGHT = size.columns, size.lines
    WIDTH = max(40, WIDTH - 2)
    HEIGHT = max(20, HEIGHT - 2)

    # 3) Пересчитываем коэффициент K1
    K1 = WIDTH * K2 * 3 / (8 * (R1 + R2))

    # 4) Отключаем ресайз/максимизацию (WinAPI)
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

    GWL_STYLE = -16
    WS_MAXIMIZEBOX = 0x00010000
    WS_SIZEBOX = 0x00040000

    hwnd = kernel32.GetConsoleWindow()
    if not hwnd:
        return

    style = user32.GetWindowLongW(hwnd, GWL_STYLE)
    style &= ~WS_MAXIMIZEBOX
    style &= ~WS_SIZEBOX
    user32.SetWindowLongW(hwnd, GWL_STYLE, style)
    user32.SetWindowPos(
        hwnd,
        None,
        0,
        0,
        0,
        0,
        0x0001 | 0x0002 | 0x0020,   # SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED
    )


def enable_ansi_colors() -> None:
    """Включает поддержку ANSI‑кодов в Windows‑консоли."""
    if platform.system() != "Windows":
        return
    kernel32 = ctypes.windll.kernel32
    STD_OUTPUT_HANDLE = -11
    h = kernel32.GetStdHandle(STD_OUTPUT_HANDLE)
    if h and h != ctypes.c_void_p(-1).value:
        mode = ctypes.c_uint()
        if kernel32.GetConsoleMode(h, ctypes.byref(mode)):
            kernel32.SetConsoleMode(h, mode.value | 0x0004)   # ENABLE_VIRTUAL_TERMINAL_PROCESSING


def move_cursor_home() -> None:
    """Перемещает курсор в левый верхний угол."""
    sys.stdout.write("\x1b[H")
    sys.stdout.flush()


# ------------------------------------------------------------
# Параметры «бублика» и палитры
# ------------------------------------------------------------
R1 = 1.0
R2 = 2.0
K2 = 8.0
K1 = 80 * K2 * 3 / (8 * (R1 + R2))   # значение будет перерасчитано в lock_console_…
WIDTH = 80
HEIGHT = 24

PALETTES = {
    "Монохром": [
        "\x1b[38;5;232m", "\x1b[38;5;236m", "\x1b[38;5;240m",
        "\x1b[38;5;244m", "\x1b[38;5;248m", "\x1b[38;5;252m",
        "\x1b[38;5;255m",
    ],
    "Океан": [
        "\x1b[38;5;15m", "\x1b[38;5;19m", "\x1b[38;5;26m",
        "\x1b[38;5;32m", "\x1b[38;5;38m", "\x1b[38;5;44m",
        "\x1b[38;5;51m",
    ],
    "Киберпанк": [
        "\x1b[38;5;198m", "\x1b[38;5;165m", "\x1b[38;5;129m",
        "\x1b[38;5;93m", "\x1b[38;5;57m", "\x1b[38;5;45m",
        "\x1b[38;5;51m",
    ],
    "Матрица": [
        "\x1b[38;5;232m", "\x1b[38;5;22m", "\x1b[38;5;28m",
        "\x1b[38;5;34m", "\x1b[38;5;40m", "\x1b[38;5;46m",
        "\x1b[38;5;154m",
    ],
    "Закат": [
        "\x1b[38;5;196m", "\x1b[38;5;202m", "\x1b[38;5;208m",
        "\x1b[38;5;214m", "\x1b[38;5;220m", "\x1b[38;5;226m",
        "\x1b[38;5;227m",
    ],
    "Радуга": [
        "\x1b[38;5;196m", "\x1b[38;5;208m", "\x1b[38;5;226m",
        "\x1b[38;5;46m", "\x1b[38;5;51m", "\x1b[38;5;63m",
        "\x1b[38;5;129m",
    ],
    "Лес": [
        "\x1b[38;5;232m", "\x1b[38;5;22m", "\x1b[38;5;28m",
        "\x1b[38;5;34m", "\x1b[38;5;82m", "\x1b[38;5;118m",
        "\x1b[38;5;154m",
    ],
    "Элджей‑вейв": [
        "\x1b[38;5;231m", "\x1b[38;5;198m", "\x1b[38;5;165m",
        "\x1b[38;5;129m", "\x1b[38;5;63m", "\x1b[38;5;39m",
        "\x1b[38;5;51m",
    ],
}
PALETTE_NAMES = list(PALETTES.keys())
PIXEL_CHAR = "@"


def luminance_to_color(L: float, palette: list) -> str:
    """Преобразует яркость ([-1, 1]) в ANSI‑цвет из палитры."""
    L_norm = (L + 1) / 2
    L_norm = max(0.0, min(1.0, L_norm))
    idx = int(L_norm * (len(palette) - 1))
    return palette[idx]


# ------------------------------------------------------------
# Предвычисление sin/cos – ускоряем рендеринг
# ------------------------------------------------------------
theta_step = 0.04
phi_step = 0.015
lookup_len = int(2 * math.pi / theta_step) + 1
SIN_LOOKUP = [math.sin(i * theta_step) for i in range(lookup_len)]
COS_LOOKUP = [math.cos(i * theta_step) for i in range(lookup_len)]


def render_frame(A: float, B: float, light_angle: float,
                 palette: list, zoom: float) -> str:
    """Рисует один кадр торуса и возвращает готовую строку."""
    k1_eff = K1 * zoom

    # Выбираем sin/cos для текущих углов вращения
    cosA = COS_LOOKUP[int((A % (2 * math.pi)) / theta_step) % lookup_len]
    sinA = SIN_LOOKUP[int((A % (2 * math.pi)) / theta_step) % lookup_len]
    cosB = COS_LOOKUP[int((B % (2 * math.pi)) / theta_step) % lookup_len]
    sinB = SIN_LOOKUP[int((B % (2 * math.pi)) / theta_step) % lookup_len]

    output = [" "] * (WIDTH * HEIGHT)
    zbuffer = [-1e9] * (WIDTH * HEIGHT)

    half_w = WIDTH / 2
    half_h = HEIGHT / 2
    aspect_ratio = 1.7

    # Параметры света (один круговой орбитальный источник)
    light_r = 6.0
    light_x = light_r * math.cos(light_angle)
    light_y = light_r * math.sin(light_angle) * 0.8
    light_z = K2 + light_r * math.sin(light_angle)

    # Матрица вращения (Rx·Rz)
    r00, r01, r02 = cosB, sinB, 0.0
    r10, r11, r12 = -cosA * sinB, cosA * cosB, -sinA
    r20, r21, r22 = -sinA * sinB, sinA * cosB, cosA

    theta = 0.0
    theta_idx = 0
    while theta < 2 * math.pi:
        costheta = COS_LOOKUP[theta_idx]
        sintheta = SIN_LOOKUP[theta_idx]

        phi = 0.0
        while phi < 2 * math.pi:
            phi_idx = int(phi / theta_step) % lookup_len
            cosphi = COS_LOOKUP[phi_idx]
            sinphi = SIN_LOOKUP[phi_idx]

            # Координаты точки на поверхности торуса
            circlex = R2 + R1 * costheta
            circley = R1 * sintheta

            x = circlex * (cosB * cosphi + sinA * sinB * sinphi) - circley * cosA * sinB
            y = circlex * (sinB * cosphi - sinA * cosB * sinphi) + circley * cosA * cosB
            z = K2 + cosA * circlex * sinphi + circley * sinA

            ooz = 1.0 / z
            xp = int(half_w + k1_eff * ooz * x * aspect_ratio)
            yp = int(half_h - k1_eff * ooz * y)

            # Нормаль поверхности
            nx = r00 * (costheta * cosphi) + r01 * sintheta + r02 * (costheta * sinphi)
            ny = r10 * (costheta * cosphi) + r11 * sintheta + r12 * (costheta * sinphi)
            nz = r20 * (costheta * cosphi) + r21 * sintheta + r22 * (costheta * sinphi)

            # Направление к свету
            dlx = light_x - x
            dly = light_y - y
            dlz = light_z - z
            dlen = math.sqrt(dlx * dlx + dly * dly + dlz * dlz)
            if dlen > 1e-3:
                dlx, dly, dlz = dlx / dlen, dly / dlen, dlz / dlen
            else:
                dlx = dly = dlz = 0.0

            # Яркость (ambient + diffuse)
            L = 0.15 + 0.85 * max(0.0, nx * dlx + ny * dly + nz * dlz)

            if 0 <= xp < WIDTH and 0 <= yp < HEIGHT:
                idx = xp + yp * WIDTH
                if ooz > zbuffer[idx]:
                    zbuffer[idx] = ooz
                    output[idx] = luminance_to_color(L, palette) + PIXEL_CHAR

            phi += phi_step
        theta += theta_step
        theta_idx = (theta_idx + 1) % lookup_len

    # --- Маркер источника света ---
    light_ooz = 1.0 / light_z
    lx = int(half_w + k1_eff * light_ooz * light_x * aspect_ratio)
    ly = int(half_h - k1_eff * light_ooz * light_y)

    if light_z < K2 - 2:
        marker = "\x1b[38;5;226m+\x1b[0m"
    elif light_z < K2:
        marker = "\x1b[38;5;220m·\x1b[0m"
    else:
        marker = "\x1b[38;5;136m·\x1b[0m"

    in_front = light_z < K2
    if 0 <= lx < WIDTH and 0 <= ly < HEIGHT:
        lidx = lx + ly * WIDTH
        if in_front:
            output[lidx] = marker
        elif output[lidx] == " ":
            output[lidx] = marker

    # --- Звезда вращающаяся вокруг бублика ---
    star_orbit_radius = R2 + R1 * 1.8
    star_angle = light_angle * 0.45
    
    star_x = star_orbit_radius * math.cos(star_angle)
    star_y = star_orbit_radius * math.sin(star_angle) * 0.7
    star_z = K2 + star_orbit_radius * math.sin(star_angle * 0.7)
    
    star_ooz = 1.0 / star_z
    sx = int(half_w + k1_eff * star_ooz * star_x * aspect_ratio)
    sy = int(half_h - k1_eff * star_ooz * star_y)
    
    if star_z < K2:
        star_marker = "\x1b[38;5;196m*\x1b[0m"
    elif star_z < K2 + 2:
        star_marker = "\x1b[38;5;208m✦\x1b[0m"
    else:
        star_marker = "\x1b[38;5;178m✧\x1b[0m"
    
    star_in_front = star_z < K2
    if 0 <= sx < WIDTH and 0 <= sy < HEIGHT:
        sidx = sx + sy * WIDTH
        if star_in_front:
            output[sidx] = star_marker
        elif output[sidx] == " ":
            output[sidx] = star_marker

    # Сборка строк
    lines = [
        "".join(output[y * WIDTH:(y + 1) * WIDTH]) for y in range(HEIGHT)
    ]
    return "\n".join(lines) + Style.RESET_ALL


# ------------------------------------------------------------
# Воспроизведение встроенного WAV‑файла без GUI‑окна
# ------------------------------------------------------------
def _play_wav_bytes(wav_bytes: bytes) -> None:
    """Кроссплатформенный «тонкий» плеер WAV‑данных."""
    if platform.system() == "Windows":
        import winsound
        # Создаем временный файл для воспроизведения
        import tempfile
        with tempfile.NamedTemporaryFile(delete=False, suffix=".wav") as f:
            f.write(wav_bytes)
            temp_path = f.name
        # Воспроизводим файл синхронно (Windows не поддерживает SND_MEMORY + SND_ASYNC)
        winsound.PlaySound(temp_path, winsound.SND_ASYNC | winsound.SND_LOOP)
        return

    # *nix‑системы: пробуем несколько популярных утилит.
    for cmd in (["aplay", "-q"], ["afplay"], ["ffplay", "-nodisp", "-autoexit", "-loglevel", "quiet"]):
        try:
            proc = subprocess.Popen(
                cmd,
                stdin=subprocess.PIPE,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            proc.stdin.write(wav_bytes)
            proc.stdin.close()
            break
        except FileNotFoundError:
            continue
    # Если ничего не найдено – звук просто не будет воспроизводиться.


def start_music_from_embedded() -> None:
    """Декодирует Base64‑строку и передаёт её в плеер."""
    wav_bytes = base64.b64decode(LOOP_WAV_B64)
    _play_wav_bytes(wav_bytes)


# ------------------------------------------------------------
# Обработчик клавиш
# ------------------------------------------------------------
def read_key():
    """Читает клавишу, возвращает логическое действие."""
    if platform.system() == "Windows":
        if msvcrt.kbhit():
            key = msvcrt.getch()
            if key in (b'\xe0', b'\x00'):          # спец. клавиши
                key2 = msvcrt.getch()
                return {72: 'up', 80: 'down', 75: 'left', 77: 'right',
                        73: 'plus', 81: 'minus'}.get(key2[0])
            ch = key.decode('ascii', errors='ignore').lower()
            if ch in ('q', '\x1b'):
                return 'quit'
            if ch in ('+', '=', ';', 'w', 'з'):
                return 'plus'
            if ch in ('-', '_', 's', 'ы'):
                return 'minus'
            if ch == '0':
                return 'reset'
            return ch
    else:
        if select.select([sys.stdin], [], [], 0) == ([sys.stdin],):
            ch = sys.stdin.read(1)
            if ch in ('q', '\x1b'):
                return 'quit'
            if ch in ('+', '=', ';', 'w', 'з'):
                return 'plus'
            if ch in ('-', '_', 's', 'ы'):
                return 'minus'
            if ch == '0':
                return 'reset'
            return ch
    return None


# ------------------------------------------------------------
# Основная функция
# ------------------------------------------------------------
def main():
    global PALETTE_NAMES, PIXEL_CHAR

    enable_ansi_colors()
    set_console_title("Donut Music | ←/→ Палитра | +/- Зум | Space Пауза | Q Выход")
    lock_console_size_and_setup_geometry(target_cols=130, target_lines=45)
    start_music_from_embedded()

    # Очистка экрана перед первым кадром
    if os.name == "nt":
        subprocess.run(["cmd", "/c", "cls"], shell=False, check=False)
    else:
        subprocess.run(["clear"], shell=False, check=False)

    A = B = 0.0
    light_angle = 0.0
    paused = False
    palette_idx = 0
    zoom = 0.55

    try:
        first = True
        while True:
            key = read_key()
            if key == 'right' or key == 'up':
                palette_idx = (palette_idx + 1) % len(PALETTE_NAMES)
            elif key == 'left' or key == 'down':
                palette_idx = (palette_idx - 1) % len(PALETTE_NAMES)
            elif key == 'plus':
                zoom = min(zoom * 1.15, 4.0)
            elif key == 'minus':
                zoom = max(zoom / 1.15, 0.25)
            elif key == 'reset':
                zoom = 1.0
            elif key in (' ', 'p', 'з'):
                paused = not paused
            elif key == 'quit':
                raise KeyboardInterrupt

            palette = PALETTES[PALETTE_NAMES[palette_idx]]
            frame = render_frame(A, B, light_angle, palette, zoom)

            status = "⏸ ПАУЗА" if paused else ""
            zoom_pct = int(zoom * 100)
            info = (
                f"\x1b[38;5;255m\x1b[48;5;0m "
                f" Палитра: {PALETTE_NAMES[palette_idx]} "
                f"[←/→] палитра  [+/-] зум:{zoom_pct}%  [0] сброс  [Space] пауза  [Q] выход "
                f"{status}\x1b[0m"
            )

            if first:
                print(frame)
                print(info)
                first = False
            else:
                move_cursor_home()
                sys.stdout.write(frame + "\n" + info)
                sys.stdout.flush()

            if not paused:
                A += 0.03
                B += 0.015
            light_angle -= 0.035
            time.sleep(0.03)

    except KeyboardInterrupt:
        print(Style.RESET_ALL)


if __name__ == "__main__":
    main()
