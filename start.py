import sys
import math
import time
import os
import platform

if platform.system() == "Windows":
    import msvcrt
    import ctypes
    from ctypes import wintypes
else:
    import select
    import termios
    import fcntl
    import struct


# ====== ОПТИМИЗАЦИЯ 1: Ультрабыстрое чтение клавиш =====
if platform.system() == "Windows":
    kernel32 = ctypes.windll.kernel32
    user32 = ctypes.windll.user32
    
    def read_key():
        """Ультрабыстрое чтение клавиши Windows."""
        if user32.GetAsyncKeyState(ord('Q')) & 0x8000 or \
           user32.GetAsyncKeyState(0x1B) & 0x8000:
            return 'quit'
        if user32.GetAsyncKeyState(ord('W')) & 0x8000 or \
           user32.GetAsyncKeyState(0x6B) & 0x8000 or \
           user32.GetAsyncKeyState(0xBB) & 0x8000:
            return 'plus'
        if user32.GetAsyncKeyState(ord('S')) & 0x8000 or \
           user32.GetAsyncKeyState(0x6D) & 0x8000 or \
           user32.GetAsyncKeyState(0xBD) & 0x8000:
            return 'minus'
        if user32.GetAsyncKeyState(ord('0')) & 0x8000:
            return 'reset'
        if user32.GetAsyncKeyState(0x26) & 0x8000:
            return 'up'
        if user32.GetAsyncKeyState(0x28) & 0x8000:
            return 'down'
        if user32.GetAsyncKeyState(0x25) & 0x8000:
            return 'left'
        if user32.GetAsyncKeyState(0x27) & 0x8000:
            return 'right'
        return None
else:
    def read_key():
        """Быстрое чтение клавиши Unix."""
        if select.select([sys.stdin], [], [], 0)[0]:
            ch = sys.stdin.read(1)
            if ch == '\x1b':
                seq = sys.stdin.read(2)
                if seq == '[A': return 'up'
                if seq == '[B': return 'down'
                if seq == '[C': return 'right'
                if seq == '[D': return 'left'
                return 'quit'
            if ch in ('q', 'Q'):
                return 'quit'
            if ch in ('+', '=', 'w', 'W', ']'):
                return 'plus'
            if ch in ('-', '_', 's', 'S', '['):
                return 'minus'
            if ch == '0':
                return 'reset'
        return None


# ====== ОПТИМИЗАЦИЯ 2: Прямой доступ к консоли =====
if platform.system() == "Windows":
    class FastConsole:
        def __init__(self):
            self.hStdOut = kernel32.GetStdHandle(-11)
            self.hStdIn = kernel32.GetStdHandle(-10)
            
            csbi = ctypes.create_string_buffer(22)
            kernel32.GetConsoleScreenBufferInfo(self.hStdOut, csbi)
            import struct
            (bufx, bufy, curx, cury, wattr,
             left, top, right, bottom, maxx, maxy) = struct.unpack("hhhhHhhhhhh", csbi.raw)
            self.width = right - left + 1
            self.height = bottom - top + 1
            
            self.buf_size = self.width * self.height
            
        def clear(self):
            coord = wintypes._COORD(0, 0)
            written = wintypes.DWORD()
            kernel32.FillConsoleOutputCharacterW(
                self.hStdOut, ord(' '), self.buf_size, coord, ctypes.byref(written)
            )
            kernel32.SetConsoleCursorPosition(self.hStdOut, coord)
            
        def write_fast(self, text):
            written = wintypes.DWORD()
            kernel32.WriteConsoleW(self.hStdOut, text, len(text), ctypes.byref(written), None)
            
        def set_cursor(self, x, y):
            coord = wintypes._COORD(x, y)
            kernel32.SetConsoleCursorPosition(self.hStdOut, coord)
            
        def hide_cursor(self):
            info = ctypes.c_ulong()
            kernel32.GetConsoleMode(self.hStdOut, ctypes.byref(info))
            kernel32.SetConsoleMode(self.hStdOut, info.value & ~0x0001)
            
else:
    class FastConsole:
        def __init__(self):
            try:
                h, w, hp, wp = struct.unpack('HHHH',
                    fcntl.ioctl(0, termios.TIOCGWINSZ,
                    struct.pack('HHHH', 0, 0, 0, 0)))
                self.width = w
                self.height = h
            except:
                self.width = 80
                self.height = 24
                
        def clear(self):
            sys.stdout.write('\033[2J\033[H')
            sys.stdout.flush()
            
        def write_fast(self, text):
            sys.stdout.write(text)
            sys.stdout.flush()
            
        def set_cursor(self, x, y):
            sys.stdout.write(f'\033[{y};{x}H')
            
        def hide_cursor(self):
            sys.stdout.write('\033[?25l')
            sys.stdout.flush()


# ====== ОПТИМИЗАЦИЯ 3: Предвычисленные таблицы =====
SIN_TABLE = {}
COS_TABLE = {}

def init_trig_tables():
    steps = 1000
    for i in range(steps):
        angle = i * 2 * math.pi / steps
        SIN_TABLE[i] = math.sin(angle)
        COS_TABLE[i] = math.cos(angle)

init_trig_tables()

def fast_sin(angle):
    idx = int((angle % (2 * math.pi)) * 1000 / (2 * math.pi)) % 1000
    return SIN_TABLE.get(idx, math.sin(angle))

def fast_cos(angle):
    idx = int((angle % (2 * math.pi)) * 1000 / (2 * math.pi)) % 1000
    return COS_TABLE.get(idx, math.cos(angle))


# ====== ОПТИМИЗАЦИЯ 4: Оптимизированный рендер donut =====
class UltraFastDonut:
    def __init__(self):
        self.console = FastConsole()
        self.console.hide_cursor()
        
        self.A = 0.0
        self.B = 0.0
        self.zoom = 1.0
        self.running = True
        
        self.R1 = 1.0
        self.R2 = 2.0
        self.K2 = 5.0
        self.theta_spacing = 0.10
        self.phi_spacing = 0.03
        
        self.luminance_chars = ".,-~:;=!*#$@"
        
        self.width = self.console.width
        self.height = self.console.height
        
        self.output = [[' ' for _ in range(self.width)] for _ in range(self.height)]
        self.zbuffer = [[0.0 for _ in range(self.width)] for _ in range(self.height)]
        
    def render_frame(self):
        A, B = self.A, self.B
        R1, R2, K2 = self.R1, self.R2, self.K2
        width, height = self.width, self.height
        chars = self.luminance_chars
        
        for y in range(height):
            for x in range(width):
                self.output[y][x] = ' '
                self.zbuffer[y][x] = 0.0
        
        cosA = fast_cos(A)
        sinA = fast_sin(A)
        cosB = fast_cos(B)
        sinB = fast_sin(B)
        
        K1 = width * K2 * 3 / (8 * (R1 + R2)) * self.zoom
        
        theta = 0.0
        theta_step = self.theta_spacing
        phi_step = self.phi_spacing
        two_pi = 2 * math.pi
        
        while theta < two_pi:
            costheta = fast_cos(theta)
            sintheta = fast_sin(theta)
            
            phi = 0.0
            while phi < two_pi:
                cosphi = fast_cos(phi)
                sinphi = fast_sin(phi)
                
                circlex = R2 + R1 * costheta
                circley = R1 * sintheta
                
                x = circlex * (cosB * cosphi + sinA * sinB * sinphi) - circley * cosA * sinB
                y = circlex * (sinB * cosphi - sinA * cosB * sinphi) + circley * cosA * cosB
                z = K2 + cosA * circlex * sinphi + circley * sinA
                
                if z > 0:
                    ooz = 1.0 / z
                    
                    xp = int(width / 2 + K1 * ooz * x)
                    yp = int(height / 2 - K1 * ooz * y / 2)
                    
                    if 0 <= xp < width and 0 <= yp < height:
                        if ooz > self.zbuffer[yp][xp]:
                            L = cosphi * costheta * sinB
                            L -= cosA * costheta * sinphi
                            L -= sinA * sintheta
                            L += cosB * (cosA * sintheta - costheta * sinA * sinphi)
                            
                            if L > 0:
                                self.zbuffer[yp][xp] = ooz
                                lum_idx = int(L * 8)
                                if lum_idx > 11:
                                    lum_idx = 11
                                if lum_idx < 0:
                                    lum_idx = 0
                                self.output[yp][xp] = chars[lum_idx]
                
                phi += phi_step
            theta += theta_step
        
        return self.output
    
    def draw_frame(self, output):
        lines = []
        for row in output:
            lines.append(''.join(row))
        
        frame_text = '\n'.join(lines)
        
        self.console.clear()
        self.console.write_fast(frame_text)
    
    def handle_input(self):
        key = read_key()
        if key == 'quit':
            self.running = False
        elif key == 'plus':
            self.zoom = min(self.zoom * 1.2, 3.0)
        elif key == 'minus':
            self.zoom = max(self.zoom / 1.2, 0.3)
        elif key == 'reset':
            self.zoom = 1.0
            self.A = 0.0
            self.B = 0.0
    
    def run(self):
        frame_count = 0
        last_time = time.time()
        target_fps = 60
        frame_time = 1.0 / target_fps
        
        try:
            while self.running:
                start_frame = time.time()
                
                self.handle_input()
                
                output = self.render_frame()
                self.draw_frame(output)
                
                self.A += 0.04
                self.B += 0.02
                
                frame_count += 1
                current_time = time.time()
                if current_time - last_time >= 1.0:
                    frame_count = 0
                    last_time = current_time
                
                elapsed = time.time() - start_frame
                if elapsed < frame_time:
                    time.sleep(frame_time - elapsed)
                    
        except KeyboardInterrupt:
            pass
        finally:
            if platform.system() == "Windows":
                self.console.set_cursor(0, self.height - 1)
            else:
                sys.stdout.write('\033[?25h\n')
            print("\nDonut stopped.")


if __name__ == "__main__":
    donut = UltraFastDonut()
    donut.run()