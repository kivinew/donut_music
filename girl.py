import os
import time
import math

# --- 3D ГЕОМЕТРИЯ МОДЕЛИ (Вершины: X, Y, Z) ---
# Силуэт девушки (Голова, торс, юбка/платье, руки, ноги)
VERTICES = [
    # Голова и шея
    (0, 2.3, 0), (0, 2.0, 0),
    # Плечи и грудь
    (-0.5, 1.7, 0), (0.5, 1.7, 0), (0, 1.4, 0.1),
    # Талия и линия таза
    (-0.3, 0.8, 0), (0.3, 0.8, 0),
    # Юбка / Платье (Нижний край)
    (-0.7, 0.0, -0.4), (0.7, 0.0, -0.4), (0.7, 0.0, 0.4), (-0.7, 0.0, 0.4),
    # Левая рука (Плечо -> Локоть -> Кисть)
    (-0.9, 1.3, 0.3), (-1.2, 1.7, 0.5),
    # Правая рука (Плечо -> Локоть -> Кисть)
    (0.9, 1.3, -0.3), (1.2, 1.7, -0.5),
    # Левая нога (Таз -> Колено -> Стопа)
    (-0.2, -0.6, -0.1), (-0.2, -1.3, 0.0),
    # Правая нога (Таз -> Колено -> Стопа)
    (0.2, -0.5, 0.1), (0.3, -1.2, -0.2)
]

# --- РЕБРА (Соединения между вершинами для отрисовки линий) ---
EDGES = [
    (0, 1), # Голова к шее
    (1, 2), (1, 3), (2, 3), # Плечевой пояс
    (2, 4), (3, 4), (4, 5), (4, 6), # Торс к талии
    (5, 6), # Талия
    # Контур юбки/платья
    (5, 7), (5, 10), (6, 8), (6, 9),
    (7, 8), (8, 9), (9, 10), (10, 7),
    # Левая рука в танце
    (2, 11), (11, 12),
    # Правая рука в танце
    (3, 13), (13, 14),
    # Левая нога
    (5, 15), (15, 16),
    # Правая нога
    (6, 17), (17, 18)
]

# Настройки размера экрана консоли
WIDTH, HEIGHT = 80, 40
FOV = 60  # Поле зрения (проекция)
DISTANCE = 3.5  # Расстояние от камеры до модели

def rotate_y(x, y, z, angle):
    """Вращение вокруг вертикальной оси Y"""
    rad = math.radians(angle)
    cos_a, sin_a = math.cos(rad), math.sin(rad)
    return x * cos_a - z * sin_a, y, x * sin_a + z * cos_a

def rotate_x(x, y, z, angle):
    """Вращение вокруг горизонтальной оси X для наклона камеры"""
    rad = math.radians(angle)
    cos_a, sin_a = math.cos(rad), math.sin(rad)
    return x, y * cos_a - z * sin_a, y * sin_a + z * cos_a

def draw_line(x0, y0, x1, y1, buffer):
    """Алгоритм Брезенхема для отрисовки линий символами в буфере"""
    dx = abs(x1 - x0)
    dy = abs(y1 - y0)
    sx = 1 if x0 < x1 else -1
    sy = 1 if y0 < y1 else -1
    err = dx - dy

    while True:
        if 0 <= x0 < WIDTH and 0 <= y0 < HEIGHT:
            buffer[y0][x0] = '#' # Символ отрисовки линий каркаса
        if x0 == x1 and y0 == y1:
            break
        e2 = 2 * err
        if e2 > -dy:
            err -= dy
            x0 += sx
        if e2 < dx:
            err += dx
            y0 += sy

def render(angle, frame_counter):
    # Инициализируем пустой экран (заполняем пробелами)
    buffer = [[' ' for _ in range(WIDTH)] for _ in range(HEIGHT)]
    projected = []

    # Динамическая деформация вершин (имитация движений танца во времени)
    dance_wave = math.sin(frame_counter * 0.2)
    dance_cos = math.cos(frame_counter * 0.15)

    for i, (x, y, z) in enumerate(VERTICES):
        # Вносим искажения в координаты рук, ног и юбки в такт времени
        if i in [11, 12]:  # Левая рука качается
            x += dance_wave * 0.2
            y += dance_cos * 0.1
        elif i in [13, 14]:  # Правая рука качается противофазно
            x -= dance_wave * 0.2
            y -= dance_cos * 0.1
        elif i in [7, 8, 9, 10]:  # Юбка колышется
            x += dance_cos * 0.08 * math.sin(i)
            z += dance_wave * 0.08 * math.cos(i)
        elif i in [15, 16, 17, 18]: # Ноги слегка пружинят
            y += abs(dance_wave) * 0.05

        # Первичное вращение объекта вокруг оси Y и наклон X
        x, y, z = rotate_y(x, y, z, angle)
        x, y, z = rotate_x(x, y, z, -10) # Легкий наклон камеры вниз

        # Математическая проекция 3D пространства на 2D плоскость экрана
        z += DISTANCE
        if z <= 0: z = 0.01
        
        # Перевод координат в индексы матрицы консоли (с учетом соотношения сторон пикселя)
        screen_x = int(WIDTH / 2 + (x * FOV / z) * 1.8)
        screen_y = int(HEIGHT / 2 - (y * FOV / z))
        projected.append((screen_x, screen_y))

    # Рисуем все линии (ребра) между вершинами
    for edge in EDGES:
        p1, p2 = projected[edge[0]], projected[edge[1]]
        draw_line(p1[0], p1[1], p2[0], p2[1], buffer)

    # Отрисовываем голову в виде объемного символа 'O'
    head = projected[0]
    if 0 <= head[0] < WIDTH and 0 <= head[1] < HEIGHT:
        buffer[head[1]][head[0]] = '@'

    # Сборка кадра в единую строку и вывод на экран
    output = '\n'.join(''.join(row) for row in buffer)
    print('\033[H' + output, end='') # Использование управляющего кода для возврата курсора

def main():
    # Настройка Windows консоли для поддержки ANSI-последовательностей (убирает мерцание)
    os.system('cls' if os.name == 'nt' else 'clear')
    
    angle = 0
    frame_counter = 0
    
    try:
        while True:
            render(angle, frame_counter)
            angle = (angle + 3) % 360 # Скорость вращения камеры вокруг модели
            frame_counter += 1
            time.sleep(0.03) # ~30 кадров в секунду для плавной анимации
    except KeyboardInterrupt:
        os.system('cls' if os.name == 'nt' else 'clear')
        print("Анимация завершена.")

if __name__ == '__main__':
    main()
