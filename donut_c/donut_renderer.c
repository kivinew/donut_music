#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <windows.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#ifdef _OPENMP
#include <omp.h>
#endif

static int WIDTH = 120;
static int HEIGHT = 40;
#define R1 1.0f
#define R2 2.0f
#define K2 5.0f
#define THETA_SPACING 0.07f
#define PHI_SPACING 0.02f
#define TWO_PI 6.28318530718f
#define BUFFER_SIZE (WIDTH * HEIGHT)
#define TABLE_SIZE 4096
#define MAX_PALETTE_SIZE 12
#define MAX_GIRL_VERTICES 64
#define MAX_GIRL_EDGES 128
#define GIRL_FOV 60.0f
#define GIRL_DISTANCE 3.5f

static float sin_table[TABLE_SIZE];
static float cos_table[TABLE_SIZE];
static int tables_initialized = 0;

static WORD current_palette[MAX_PALETTE_SIZE];
static int palette_size = 7;

static float girl_vertices[MAX_GIRL_VERTICES][3];
static int girl_vertex_count = 0;
static int girl_edges[MAX_GIRL_EDGES][2];
static int girl_edge_count = 0;

// DVD стиль движения бублика
static float donut_pos_x = 0.0f;
static float donut_pos_y = 0.0f;
static float object_pos_x = 0.0f;
static float object_pos_y = 0.0f;
static float donut_vel_x = 0.35f;
static float donut_vel_y = 0.2f;
static const float DONUT_BORDER_MARGIN = 15.0f;

static CHAR_INFO* screenBuffer = NULL;
static float* zbuffer = NULL;

static HANDLE hStdOut = NULL;
static HANDLE hConsoleBuffer = NULL;
static COORD bufferSize;
static COORD bufferCoord = {0, 0};
static SMALL_RECT writeRegion;

static const char luminance_chars[] = ".,-~:;=!*#$@";

static const float DEFAULT_GIRL_VERTICES[MAX_GIRL_VERTICES][3] = {
    {0.0f, 2.3f, 0.0f}, {0.0f, 2.0f, 0.0f},
    {-0.5f, 1.7f, 0.0f}, {0.5f, 1.7f, 0.0f}, {0.0f, 1.4f, 0.1f},
    {-0.3f, 0.8f, 0.0f}, {0.3f, 0.8f, 0.0f},
    {-0.7f, 0.0f, -0.4f}, {0.7f, 0.0f, -0.4f}, {0.7f, 0.0f, 0.4f}, {-0.7f, 0.0f, 0.4f},
    {-0.9f, 1.3f, 0.3f}, {-1.2f, 1.7f, 0.5f},
    {0.9f, 1.3f, -0.3f}, {1.2f, 1.7f, -0.5f},
    {-0.2f, -0.6f, -0.1f}, {-0.2f, -1.3f, 0.0f},
    {0.2f, -0.5f, 0.1f}, {0.3f, -1.2f, -0.2f}
};

static const int DEFAULT_GIRL_EDGES[MAX_GIRL_EDGES][2] = {
    {0,1}, {1,2}, {1,3}, {2,3}, {2,4}, {3,4}, {4,5}, {4,6}, {5,6},
    {5,7}, {5,10}, {6,8}, {6,9}, {7,8}, {8,9}, {9,10}, {10,7},
    {2,11}, {11,12}, {3,13}, {13,14}, {5,15}, {15,16}, {6,17}, {17,18}
};

static inline void load_default_girl_model(void) {
    if (girl_vertex_count > 0) return;
    girl_vertex_count = 19;
    girl_edge_count = 24;
    for (int i = 0; i < girl_vertex_count; i++) {
        for (int j = 0; j < 3; j++) {
            girl_vertices[i][j] = DEFAULT_GIRL_VERTICES[i][j];
        }
    }
    for (int i = 0; i < girl_edge_count; i++) {
        girl_edges[i][0] = DEFAULT_GIRL_EDGES[i][0];
        girl_edges[i][1] = DEFAULT_GIRL_EDGES[i][1];
    }
}

static inline void __forceinline init_tables(void) {
    int i;
    if (tables_initialized) return;
    #pragma omp parallel for schedule(static)
    for (i = 0; i < TABLE_SIZE; i++) {
        float angle = (float)i * TWO_PI / (float)TABLE_SIZE;
        sin_table[i] = sinf(angle);
        cos_table[i] = cosf(angle);
    }
    tables_initialized = 1;
}

static inline float __forceinline fast_sin(float angle) {
    int idx = (int)((fmodf(angle, TWO_PI) * (float)TABLE_SIZE) / TWO_PI) & (TABLE_SIZE - 1);
    return sin_table[idx];
}

static inline float __forceinline fast_cos(float angle) {
    int idx = (int)((fmodf(angle, TWO_PI) * (float)TABLE_SIZE) / TWO_PI) & (TABLE_SIZE - 1);
    return cos_table[idx];
}

static int __forceinline init_console(void) {
    hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hStdOut == INVALID_HANDLE_VALUE) return 0;
    
    hConsoleBuffer = CreateConsoleScreenBuffer(
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, CONSOLE_TEXTMODE_BUFFER, NULL
    );
    if (hConsoleBuffer == INVALID_HANDLE_VALUE) return 0;
    
    SetConsoleActiveScreenBuffer(hConsoleBuffer);
    
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(hConsoleBuffer, &cursorInfo);
    cursorInfo.bVisible = FALSE;
    SetConsoleCursorInfo(hConsoleBuffer, &cursorInfo);
    
    // ✨ Автоматически определяем максимальный размер окна консоли
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hStdOut, &csbi);
    
    WIDTH = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    HEIGHT = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    
    // Максимально увеличиваем окно
    SMALL_RECT windowSize = {0, 0, 1, 1};
    SetConsoleWindowInfo(hConsoleBuffer, TRUE, &windowSize);
    
    COORD maxSize = GetLargestConsoleWindowSize(hConsoleBuffer);
    WIDTH = maxSize.X;
    HEIGHT = maxSize.Y - 1;
    
    // Устанавливаем буфер и размер окна
    bufferSize.X = WIDTH;
    bufferSize.Y = HEIGHT;
    SetConsoleScreenBufferSize(hConsoleBuffer, bufferSize);
    
    writeRegion.Left = 0;
    writeRegion.Top = 0;
    writeRegion.Right = WIDTH - 1;
    writeRegion.Bottom = HEIGHT - 1;
    
    SetConsoleWindowInfo(hConsoleBuffer, TRUE, &writeRegion);
    
    // Выделяем память для буферов динамически
    screenBuffer = (CHAR_INFO*)malloc(WIDTH * HEIGHT * sizeof(CHAR_INFO));
    zbuffer = (float*)malloc(WIDTH * HEIGHT * sizeof(float));
    
    // Палитра по умолчанию - Океан
    static const WORD default_palette[] = {0x0F, 0x01, 0x09, 0x0B, 0x0B, 0x0B, 0x0B};
    memcpy(current_palette, default_palette, sizeof(WORD) * 7);
    
    init_tables();
    return 1;
}

static void __forceinline cleanup_console(void) {
    if (hStdOut != NULL && hStdOut != INVALID_HANDLE_VALUE) {
        SetConsoleActiveScreenBuffer(hStdOut);
    }
    if (hConsoleBuffer != NULL && hConsoleBuffer != INVALID_HANDLE_VALUE) {
        CloseHandle(hConsoleBuffer);
        hConsoleBuffer = NULL;
    }
}

static inline void project_point(float x, float y, float z, float K1, int *sx, int *sy, float *ooz) {
    float depth = K2 + z;
    if (depth <= 0.0f) {
        *sx = -1;
        *sy = -1;
        *ooz = 0.0f;
        return;
    }

    *ooz = 1.0f / depth;
    *sx = (int)((float)WIDTH * 0.5f + object_pos_x + K1 * (*ooz) * x * 2.0f);
    *sy = (int)((float)HEIGHT * 0.5f + object_pos_y - K1 * (*ooz) * y);
}

static inline void draw_screen_pixel(int sx, int sy, float ooz, char ch, WORD attr) {
    if (sx < 0 || sx >= WIDTH || sy < 0 || sy >= HEIGHT) return;
    int idx = sy * WIDTH + sx;
    if (ooz > zbuffer[idx]) {
        zbuffer[idx] = ooz;
        screenBuffer[idx].Char.AsciiChar = ch;
        screenBuffer[idx].Attributes = attr;
    }
}

static inline void draw_point_3d(float x, float y, float z, float K1, char ch, WORD attr) {
    int sx, sy;
    float ooz;
    project_point(x, y, z, K1, &sx, &sy, &ooz);
    draw_screen_pixel(sx, sy, ooz, ch, attr);
}

static inline void draw_line_3d(float x1, float y1, float z1, float x2, float y2, float z2, float K1, char ch, WORD attr) {
    int sx1, sy1, sx2, sy2;
    float ooz1, ooz2;
    project_point(x1, y1, z1, K1, &sx1, &sy1, &ooz1);
    project_point(x2, y2, z2, K1, &sx2, &sy2, &ooz2);
    if (sx1 < 0 || sy1 < 0 || sx2 < 0 || sy2 < 0) return;

    int dx = abs(sx2 - sx1);
    int sx = sx1 < sx2 ? 1 : -1;
    int dy = -abs(sy2 - sy1);
    int sy = sy1 < sy2 ? 1 : -1;
    int err = dx + dy;
    int x = sx1;
    int y = sy1;

    while (1) {
        float t;
        if (dx == 0 && abs(sy2 - sy1) == 0) {
            t = 0.0f;
        } else {
            float denom = (float)(dx + abs(sy2 - sy1));
            t = denom > 0.0f ? (float)(abs(x - sx1) + abs(y - sy1)) / denom : 0.0f;
        }
        float ooz = ooz1 + (ooz2 - ooz1) * t;
        draw_screen_pixel(x, y, ooz, ch, attr);

        if (x == sx2 && y == sy2) break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y += sy;
        }
    }
}

static inline void rotate_y_point(float *x, float *z, float angle) {
    float c = fast_cos(angle);
    float s = fast_sin(angle);
    float rx = (*x) * c + (*z) * s;
    float rz = -(*x) * s + (*z) * c;
    *x = rx;
    *z = rz;
}

static inline void rotate_x_point(float *y, float *z, float angle) {
    float c = fast_cos(angle);
    float s = fast_sin(angle);
    float ry = (*y) * c - (*z) * s;
    float rz = (*y) * s + (*z) * c;
    *y = ry;
    *z = rz;
}

static inline void draw_screen_line(int x0, int y0, float ooz0, int x1, int y1, float ooz1, char ch, WORD attr) {
    if (x0 < 0 || x0 >= WIDTH || y0 < 0 || y0 >= HEIGHT || x1 < 0 || x1 >= WIDTH || y1 < 0 || y1 >= HEIGHT) return;

    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int x = x0;
    int y = y0;
    int steps = dx + abs(y1 - y0);
    int step = 0;

    while (1) {
        float t = steps > 0 ? (float)step / (float)steps : 0.0f;
        float ooz = ooz0 + (ooz1 - ooz0) * t;
        draw_screen_pixel(x, y, ooz, ch, attr);

        if (x == x1 && y == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y += sy;
        }
        step++;
    }
}

static inline void draw_girl_pixel(int x, int y, char ch, WORD attr) {
    draw_screen_pixel(x, y, 1.0f, ch, attr);
}

static inline void draw_girl_circle(int cx, int cy, int r, char ch, WORD attr, int fill) {
    for (int y = cy - r; y <= cy + r; y++) {
        for (int x = cx - r; x <= cx + r; x++) {
            int dx = x - cx;
            int dy = y - cy;
            int dist2 = dx * dx + dy * dy;
            if (fill ? dist2 <= r * r : dist2 <= r * r && dist2 > (r - 1) * (r - 1)) {
                draw_girl_pixel(x, y, ch, attr);
            }
        }
    }
}

static inline void draw_girl_thick_line(int x1, int y1, int x2, int y2, int thickness, char ch, WORD attr) {
    int dx = abs(x2 - x1);
    int sx = x1 < x2 ? 1 : -1;
    int dy = -abs(y2 - y1);
    int sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;
    int x = x1;
    int y = y1;

    while (1) {
        for (int oy = -thickness; oy <= thickness; oy++) {
            for (int ox = -thickness; ox <= thickness; ox++) {
                if (ox * ox + oy * oy <= thickness * thickness) {
                    draw_girl_pixel(x + ox, y + oy, ch, attr);
                }
            }
        }

        if (x == x2 && y == y2) break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y += sy;
        }
    }
}

static inline void draw_girl_dress(int cx, int waist_y, int hem_y, int half_waist, int half_hem, WORD attr) {
    for (int y = waist_y; y <= hem_y; y++) {
        float t = (float)(y - waist_y) / (float)(hem_y - waist_y + 1);
        int half = (int)(half_waist + (half_hem - half_waist) * t);
        for (int x = cx - half; x <= cx + half; x++) {
            char ch = (x - cx) % 3 == 0 ? '=' : '#';
            draw_girl_pixel(x, y, ch, attr);
        }
    }

    int wave = (int)(2.0f * fast_sin((float)hem_y * 0.7f));
    for (int x = cx - half_hem - 2; x <= cx + half_hem + 2; x++) {
        int y = hem_y + (int)(fast_sin((float)(x - cx) * 0.9f) * 1.5f) + wave / 3;
        draw_girl_pixel(x, y, '~', 0x0E);
    }
}

static void draw_girl_projection(float light_time) {
    int scale = HEIGHT / 42;
    if (scale < 1) scale = 1;
    if (scale > 2) scale = 2;

    int cx = (int)((float)WIDTH * 0.5f + object_pos_x);
    int base_y = (int)((float)HEIGHT * 0.76f + object_pos_y);
    float beat = fast_sin(light_time * 5.2f);
    float sway = fast_sin(light_time * 2.1f);
    int bounce = (int)(beat * 1.2f * (float)scale);

    int head_y = base_y - 22 * scale + bounce;
    int neck_y = base_y - 16 * scale + bounce;
    int chest_y = base_y - 12 * scale + bounce;
    int waist_y = base_y - 7 * scale + bounce;
    int hem_y = base_y + 8 * scale + bounce;
    int hip_y = waist_y + 1 * scale;
    int shoulder_y = chest_y - 1 * scale;
    int shoulder_half = 5 * scale;

    draw_girl_pixel(cx - 1 * scale, base_y + 2 * scale, '"', 0x08);
    draw_girl_pixel(cx + 1 * scale, base_y + 2 * scale, '"', 0x08);

    int head_r = 3 * scale;
    draw_girl_circle(cx, head_y - head_r, head_r, '#', 0x04, 1);
    draw_girl_circle(cx, head_y - head_r, head_r - 1, '@', 0x0F, 1);
    draw_girl_pixel(cx - 1 * scale, head_y - head_r, '#', 0x04);
    draw_girl_pixel(cx + 1 * scale, head_y - head_r, '#', 0x04);
    draw_girl_pixel(cx - 1 * scale, head_y - head_r + 1, 'o', 0x0F);
    draw_girl_pixel(cx + 1 * scale, head_y - head_r + 1, 'o', 0x0F);
    draw_girl_pixel(cx, head_y - head_r + 3, '.', 0x04);

    draw_girl_thick_line(cx, neck_y, cx, chest_y, scale, '#', 0x0F);
    draw_girl_thick_line(cx - shoulder_half, shoulder_y, cx + shoulder_half, shoulder_y, scale, '#', 0x0F);

    int left_hand_x = cx - 9 * scale - (int)(sway * 2.0f * (float)scale);
    int left_hand_y = base_y - 13 * scale + (int)(fast_cos(light_time * 4.4f) * 2.0f * (float)scale) + bounce;
    int right_hand_x = cx + 9 * scale - (int)(sway * 2.0f * (float)scale);
    int right_hand_y = base_y - 15 * scale + (int)(fast_sin(light_time * 4.4f) * 2.0f * (float)scale) + bounce;
    int left_elbow_x = cx - 7 * scale - (int)(beat * 1.5f * (float)scale);
    int left_elbow_y = base_y - 9 * scale + (int)(fast_cos(light_time * 3.0f) * 1.5f * (float)scale) + bounce;
    int right_elbow_x = cx + 7 * scale - (int)(beat * 1.5f * (float)scale);
    int right_elbow_y = base_y - 10 * scale + (int)(fast_sin(light_time * 3.0f) * 1.5f * (float)scale) + bounce;

    draw_girl_thick_line(cx - shoulder_half, shoulder_y, left_elbow_x, left_elbow_y, scale, '#', 0x0A);
    draw_girl_thick_line(left_elbow_x, left_elbow_y, left_hand_x, left_hand_y, scale, '#', 0x0A);
    draw_girl_thick_line(cx + shoulder_half, shoulder_y, right_elbow_x, right_elbow_y, scale, '#', 0x0A);
    draw_girl_thick_line(right_elbow_x, right_elbow_y, right_hand_x, right_hand_y, scale, '#', 0x0A);
    draw_girl_circle(left_hand_x, left_hand_y, scale, '*', 0x0E, 1);
    draw_girl_circle(right_hand_x, right_hand_y, scale, '*', 0x0E, 1);

    draw_girl_dress(cx, waist_y, hem_y, 4 * scale, 10 * scale + (int)(fabsf(sway) * 2.0f * (float)scale), 0x0D);
    draw_girl_thick_line(cx - 2 * scale, hip_y, cx + 2 * scale, hip_y, scale, '#', 0x0F);

    int left_knee_x = cx - 3 * scale + (int)(beat * 1.4f * (float)scale);
    int left_knee_y = base_y + 2 * scale - (int)(fabsf(beat) * 1.0f * (float)scale);
    int right_knee_x = cx + 3 * scale - (int)(beat * 1.4f * (float)scale);
    int right_knee_y = base_y + 2 * scale + (int)(fabsf(beat) * 1.0f * (float)scale);
    int left_foot_x = cx - 6 * scale + (int)(fast_cos(light_time * 2.6f) * 2.0f * (float)scale);
    int left_foot_y = base_y + 6 * scale + (int)(fast_sin(light_time * 2.6f) * 1.0f * (float)scale);
    int right_foot_x = cx + 6 * scale - (int)(fast_cos(light_time * 2.6f) * 2.0f * (float)scale);
    int right_foot_y = base_y + 6 * scale - (int)(fast_sin(light_time * 2.6f) * 1.0f * (float)scale);

    draw_girl_thick_line(cx - 2 * scale, hip_y, left_knee_x, left_knee_y, scale, '#', 0x0A);
    draw_girl_thick_line(left_knee_x, left_knee_y, left_foot_x, left_foot_y, scale, '#', 0x0A);
    draw_girl_thick_line(cx + 2 * scale, hip_y, right_knee_x, right_knee_y, scale, '#', 0x0A);
    draw_girl_thick_line(right_knee_x, right_knee_y, right_foot_x, right_foot_y, scale, '#', 0x0A);
    draw_girl_thick_line(left_foot_x - 2 * scale, left_foot_y, left_foot_x + 2 * scale, left_foot_y, scale, '#', 0x0E);
    draw_girl_thick_line(right_foot_x - 2 * scale, right_foot_y, right_foot_x + 2 * scale, right_foot_y, scale, '#', 0x0E);

    for (int x = cx - 13 * scale; x <= cx + 13 * scale; x++) {
        int y = base_y + 8 * scale + (int)(fast_sin((float)(x - cx) * 0.8f) * 0.6f);
        draw_girl_pixel(x, y, '.', 0x08);
    }
}

static inline void draw_sphere_3d(float x, float y, float z, float radius, float K1, char ch, WORD attr) {
    int cx, cy;
    float center_ooz;
    project_point(x, y, z, K1, &cx, &cy, &center_ooz);
    if (cx < 0 || cy < 0) return;

    int r = (int)(K1 * radius * 2.0f * center_ooz);
    if (r < 1) r = 1;

    for (int py = -r; py <= r; py++) {
        for (int px = -r; px <= r; px++) {
            float d2 = (float)(px * px + py * py);
            if (d2 > (float)(r * r)) continue;

            float depth_z = z + sqrtf(fmaxf(0.0f, radius * radius - d2 * radius * radius / (float)(r * r)));
            int sx, sy;
            float ooz;
            project_point(x + (float)px * radius / (float)r, y + (float)py * radius / (float)r, depth_z, K1, &sx, &sy, &ooz);
            if (sx < 0 || sy < 0) continue;

            float shade = 0.35f + 0.65f * (1.0f - sqrtf(d2) / (float)r);
            char body = ch;
            if (ch == '@') {
                const char sun_chars[] = ".:-=+*#%@";
                body = sun_chars[(int)(shade * 8.9f)];
            }
            draw_screen_pixel(sx, sy, ooz, body, attr);
        }
    }
}

static void draw_solar_system(float K1, float light_time) {
    draw_sphere_3d(0.0f, 0.0f, 0.0f, 0.55f, K1, '@', 0x0E);

    float orbit_radii[] = {0.95f, 1.35f, 1.75f, 2.25f, 2.75f, 3.25f};
    float planet_radii[] = {0.13f, 0.18f, 0.20f, 0.25f, 0.31f, 0.28f};
    float speeds[] = {2.2f, 1.65f, 1.25f, 0.95f, 0.70f, 0.52f};
    float phases[] = {0.0f, 1.1f, 2.4f, 3.6f, 4.7f, 5.5f};
    float tilts[] = {0.05f, 0.18f, -0.12f, 0.28f, -0.22f, 0.36f};
    WORD colors[] = {0x07, 0x0B, 0x0F, 0x0C, 0x0A, 0x06};

    for (int i = 0; i < 6; i++) {
        float r = orbit_radii[i];
        for (int j = 0; j < 120; j++) {
            float a = (float)j / 120.0f * TWO_PI;
            float x = r * fast_cos(a);
            float z = r * fast_sin(a);
            float y = fast_sin(a + tilts[i]) * 0.18f;
            rotate_y_point(&x, &z, tilts[i] * 0.7f);
            draw_point_3d(x, y, z, K1, '.', 0x08);
        }

        float a = light_time * speeds[i] + phases[i];
        float x = r * fast_cos(a);
        float z = r * fast_sin(a);
        float y = fast_sin(a * 1.7f + tilts[i]) * 0.22f;
        rotate_y_point(&x, &z, tilts[i]);
        draw_sphere_3d(x, y, z, planet_radii[i], K1, 'o', colors[i]);
    }

    for (int j = 0; j < 180; j++) {
        float a = (float)j / 180.0f * TWO_PI;
        float x = 3.7f * fast_cos(a);
        float z = 3.7f * fast_sin(a);
        float y = fast_sin(a * 2.0f) * 0.25f;
        rotate_y_point(&x, &z, 0.45f);
        draw_point_3d(x, y, z, K1, j % 9 == 0 ? '*' : '.', 0x0F);
    }
}

static void draw_line_with_rotation(float x1, float y1, float z1, float x2, float y2, float z2, float K1, char ch, WORD attr, float angle) {
    rotate_y_point(&x1, &z1, angle);
    rotate_y_point(&x2, &z2, angle);
    draw_line_3d(x1, y1, z1, x2, y2, z2, K1, ch, attr);
}

static void draw_sphere_with_rotation(float x, float y, float z, float radius, float K1, char ch, WORD attr, float angle) {
    rotate_y_point(&x, &z, angle);
    draw_sphere_3d(x, y, z, radius, K1, ch, attr);
}

static void draw_dancing_girl(float K1, float light_time) {
    load_default_girl_model();

    int frame_counter = (int)(light_time * 30.0f);
    float dance_wave = fast_sin((float)frame_counter * 0.2f);
    float dance_cos = fast_cos((float)frame_counter * 0.15f);
    float angle = (float)frame_counter * 3.0f * TWO_PI / 360.0f;
    float camera_tilt = -10.0f * TWO_PI / 360.0f;

    int sx[MAX_GIRL_VERTICES];
    int sy[MAX_GIRL_VERTICES];
    float ooz[MAX_GIRL_VERTICES];
    int valid[MAX_GIRL_VERTICES];

    for (int i = 0; i < girl_vertex_count; i++) {
        float x = girl_vertices[i][0];
        float y = girl_vertices[i][1];
        float z = girl_vertices[i][2];

        if (i == 11 || i == 12) {
            x += dance_wave * 0.2f;
            y += dance_cos * 0.1f;
        } else if (i == 13 || i == 14) {
            x -= dance_wave * 0.2f;
            y -= dance_cos * 0.1f;
        } else if (i >= 7 && i <= 10) {
            x += dance_cos * 0.08f * fast_sin((float)i);
            z += dance_wave * 0.08f * fast_cos((float)i);
        } else if (i >= 15 && i <= 18) {
            y += fabsf(dance_wave) * 0.05f;
        }

        rotate_y_point(&x, &z, angle);
        rotate_x_point(&y, &z, camera_tilt);
        z += GIRL_DISTANCE;
        if (z <= 0.0f) z = 0.01f;

        sx[i] = (int)((float)WIDTH * 0.5f + object_pos_x + (x * GIRL_FOV / z) * 1.8f);
        sy[i] = (int)((float)HEIGHT * 0.5f + object_pos_y - (y * GIRL_FOV / z));
        ooz[i] = 1.0f / z;
        valid[i] = sx[i] >= 0 && sx[i] < WIDTH && sy[i] >= 0 && sy[i] < HEIGHT;
    }

    for (int i = 0; i < girl_edge_count; i++) {
        int a = girl_edges[i][0];
        int b = girl_edges[i][1];
        if (a < 0 || b < 0 || a >= girl_vertex_count || b >= girl_vertex_count) continue;
        draw_screen_line(sx[a], sy[a], ooz[a], sx[b], sy[b], ooz[b], '#', 0x0B);
    }

    if (girl_vertex_count > 0 && valid[0]) {
        draw_screen_pixel(sx[0], sy[0], ooz[0], '@', 0x0F);
    }
}

static void draw_cube(float K1, float light_time) {
    float a = light_time * 0.7f;
    float b = light_time * 0.45f;
    float ca = fast_cos(a), sa = fast_sin(a);
    float cb = fast_cos(b), sb = fast_sin(b);
    float verts[8][3] = {
        {-1.0f, -1.0f, -1.0f}, { 1.0f, -1.0f, -1.0f}, { 1.0f,  1.0f, -1.0f}, {-1.0f,  1.0f, -1.0f},
        {-1.0f, -1.0f,  1.0f}, { 1.0f, -1.0f,  1.0f}, { 1.0f,  1.0f,  1.0f}, {-1.0f,  1.0f,  1.0f}
    };

    for (int i = 0; i < 8; i++) {
        float x = verts[i][0], y = verts[i][1], z = verts[i][2];
        float rx = x * cb + z * sb;
        float rz = -x * sb + z * cb;
        x = rx; z = rz;
        float ry = y * ca - z * sa;
        rz = y * sa + z * ca;
        y = ry; z = rz;
        for (int k = 0; k < 3; k++) verts[i][k] = (k == 0 ? x : (k == 1 ? y : z)) * 0.55f;
    }

    int edges[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    for (int i = 0; i < 12; i++) {
        int a = edges[i][0], b = edges[i][1];
        draw_line_3d(verts[a][0], verts[a][1], verts[a][2], verts[b][0], verts[b][1], verts[b][2], K1, '#', 0x0B);
    }
    for (int i = 0; i < 8; i++) {
        draw_sphere_3d(verts[i][0], verts[i][1], verts[i][2], 0.06f, K1, '*', 0x0F);
    }
}

static void draw_heart(float K1, float light_time) {
    for (int i = 0; i < 72; i++) {
        float t = (float)i / 72.0f * TWO_PI;
        float st = fast_sin(t);
        float ct = fast_cos(t);
        float base_x = 16.0f * st * st * st;
        float base_y = 13.0f * ct - 5.0f * fast_cos(2.0f * t) - 2.0f * fast_cos(3.0f * t) - fast_cos(4.0f * t);
        for (int j = 0; j < 28; j++) {
            float p = (float)j / 28.0f * TWO_PI;
            float x = base_x * (0.75f + 0.25f * fast_sin(p));
            float y = base_y * (0.75f + 0.25f * fast_cos(p));
            float z = 6.0f * st * fast_sin(p);
            rotate_y_point(&x, &z, light_time * 0.55f);
            draw_point_3d(x * 0.065f, y * 0.065f - 0.05f, z * 0.065f, K1, '.', 0x0C);
        }
    }
}

static void draw_galaxy(float K1, float light_time) {
    for (int arm = 0; arm < 4; arm++) {
        WORD color = arm % 2 ? 0x0B : 0x0D;
        for (int i = 0; i < 140; i++) {
            float t = (float)i / 140.0f;
            float angle = t * TWO_PI * 1.85f + (float)arm * TWO_PI / 4.0f + light_time * 0.18f;
            float radius = 0.35f + t * 3.2f;
            float spread = fast_sin(t * 18.0f + (float)arm) * 0.16f * (0.4f + t);
            float x = radius * fast_cos(angle) + fast_cos(angle + TWO_PI / 2.0f) * spread;
            float z = radius * fast_sin(angle) + fast_sin(angle + TWO_PI / 2.0f) * spread;
            float y = fast_sin(t * 12.0f + (float)arm * 0.7f) * 0.18f;
            rotate_y_point(&x, &z, 0.25f * fast_sin(light_time * 0.2f));
            draw_point_3d(x, y, z, K1, i % 11 == 0 ? '*' : '.', color);
        }
    }
    draw_sphere_3d(0.0f, 0.0f, 0.0f, 0.28f, K1, '@', 0x0E);
}

// === Python callable functions ===

static PyObject* donut_set_palette(PyObject* self, PyObject* args) {
    PyObject* list_obj;
    if (!PyArg_ParseTuple(args, "O", &list_obj)) {
        return NULL;
    }

    if (!PyList_Check(list_obj)) {
        PyErr_SetString(PyExc_TypeError, "Expected list of colors");
        return NULL;
    }

    Py_ssize_t len = PyList_Size(list_obj);
    if (len > MAX_PALETTE_SIZE) len = MAX_PALETTE_SIZE;

    for (Py_ssize_t i = 0; i < len; i++) {
        PyObject* item = PyList_GetItem(list_obj, i);
        current_palette[i] = (WORD)PyLong_AsLong(item);
    }

    palette_size = (int)len;
    Py_RETURN_NONE;
}

static PyObject* donut_set_girl_model(PyObject* self, PyObject* args) {
    PyObject* vertices_obj;
    PyObject* edges_obj;

    if (!PyArg_ParseTuple(args, "OO", &vertices_obj, &edges_obj)) {
        return NULL;
    }

    if (!PySequence_Check(vertices_obj) || !PySequence_Check(edges_obj)) {
        PyErr_SetString(PyExc_TypeError, "Expected girl vertices and edges sequences");
        return NULL;
    }

    Py_ssize_t vertex_count = PySequence_Size(vertices_obj);
    Py_ssize_t edge_count = PySequence_Size(edges_obj);
    if (vertex_count > MAX_GIRL_VERTICES) vertex_count = MAX_GIRL_VERTICES;
    if (edge_count > MAX_GIRL_EDGES) edge_count = MAX_GIRL_EDGES;

    for (Py_ssize_t i = 0; i < vertex_count; i++) {
        PyObject* vertex_obj = PySequence_GetItem(vertices_obj, i);
        if (!vertex_obj || !PySequence_Check(vertex_obj) || PySequence_Size(vertex_obj) < 3) {
            Py_XDECREF(vertex_obj);
            PyErr_SetString(PyExc_TypeError, "Expected each girl vertex to be a 3-item sequence");
            return NULL;
        }

        PyObject* x_obj = PySequence_GetItem(vertex_obj, 0);
        PyObject* y_obj = PySequence_GetItem(vertex_obj, 1);
        PyObject* z_obj = PySequence_GetItem(vertex_obj, 2);
        if (!x_obj || !y_obj || !z_obj) {
            Py_XDECREF(vertex_obj);
            Py_XDECREF(x_obj);
            Py_XDECREF(y_obj);
            Py_XDECREF(z_obj);
            PyErr_SetString(PyExc_TypeError, "Could not read girl vertex coordinates");
            return NULL;
        }

        girl_vertices[i][0] = (float)PyFloat_AsDouble(x_obj);
        girl_vertices[i][1] = (float)PyFloat_AsDouble(y_obj);
        girl_vertices[i][2] = (float)PyFloat_AsDouble(z_obj);

        Py_DECREF(x_obj);
        Py_DECREF(y_obj);
        Py_DECREF(z_obj);
        Py_DECREF(vertex_obj);

        if (PyErr_Occurred()) {
            return NULL;
        }
    }

    for (Py_ssize_t i = 0; i < edge_count; i++) {
        PyObject* edge_obj = PySequence_GetItem(edges_obj, i);
        if (!edge_obj || !PySequence_Check(edge_obj) || PySequence_Size(edge_obj) < 2) {
            Py_XDECREF(edge_obj);
            PyErr_SetString(PyExc_TypeError, "Expected each girl edge to be a 2-item sequence");
            return NULL;
        }

        PyObject* a_obj = PySequence_GetItem(edge_obj, 0);
        PyObject* b_obj = PySequence_GetItem(edge_obj, 1);
        if (!a_obj || !b_obj) {
            Py_XDECREF(edge_obj);
            Py_XDECREF(a_obj);
            Py_XDECREF(b_obj);
            PyErr_SetString(PyExc_TypeError, "Could not read girl edge indexes");
            return NULL;
        }

        long a = PyLong_AsLong(a_obj);
        long b = PyLong_AsLong(b_obj);
        if (PyErr_Occurred()) {
            Py_DECREF(a_obj);
            Py_DECREF(b_obj);
            Py_DECREF(edge_obj);
            return NULL;
        }

        girl_edges[i][0] = (int)a;
        girl_edges[i][1] = (int)b;

        Py_DECREF(a_obj);
        Py_DECREF(b_obj);
        Py_DECREF(edge_obj);
    }

    girl_vertex_count = (int)vertex_count;
    girl_edge_count = (int)edge_count;
    Py_RETURN_NONE;
}

static PyObject* donut_render_frame(PyObject* self, PyObject* args) {
    float A, B, light_time, zoom;
    int shape_id = 0;

    if (!PyArg_ParseTuple(args, "ffff|i", &A, &B, &light_time, &zoom, &shape_id)) {
        return NULL;
    }

    if (!hConsoleBuffer) {
        PyErr_SetString(PyExc_RuntimeError, "Console not initialized");
        return NULL;
    }

    // Очистка буферов на весь размер
    memset(screenBuffer, 0, WIDTH * HEIGHT * sizeof(CHAR_INFO));
    memset(zbuffer, 0, WIDTH * HEIGHT * sizeof(float));

    const float K1 = (float)HEIGHT * K2 * 3.0f / (4.0f * (R1 + R2)) * zoom;

    if (shape_id == 0) {
        object_pos_x = donut_pos_x;
        object_pos_y = donut_pos_y;
    } else if (shape_id == 1 || shape_id == 3 || shape_id == 5) {
        object_pos_x = fast_sin(light_time * 0.35f) * (float)WIDTH * 0.18f;
        object_pos_y = fast_cos(light_time * 0.27f) * (float)HEIGHT * 0.12f;
    } else {
        object_pos_x = 0.0f;
        object_pos_y = 0.0f;
    }

    bufferSize.X = WIDTH;
    bufferSize.Y = HEIGHT;
    writeRegion.Right = WIDTH - 1;
    writeRegion.Bottom = HEIGHT - 1;

    switch (shape_id) {
        case 1:
            draw_solar_system(K1, light_time);
            break;
        case 2:
            draw_girl_projection(light_time);
            break;
        case 3:
            draw_cube(K1, light_time);
            break;
        case 4:
            draw_heart(K1, light_time);
            break;
        case 5:
            draw_galaxy(K1, light_time);
            break;
        case 0:
        default:
            {
                const float cosA = fast_cos(A);
                const float sinA = fast_sin(A);
                const float cosB = fast_cos(B);
                const float sinB = fast_sin(B);

                // ✨ Улучшенная динамическая система освещения
                const float light_orbit = light_time * 0.7f;
                const float light_height = 0.5f + 0.4f * fast_sin(light_time * 0.3f);
                const float light_intensity = 0.8f + 0.2f * fast_sin(light_time * 0.5f);

                const float lx = fast_sin(light_orbit) * 0.85f;
                const float ly = fast_cos(light_orbit) * 0.85f;
                const float lz = light_height;

                const float theta_step = THETA_SPACING;
                const float phi_step = PHI_SPACING;

                for (float theta = 0.0f; theta < TWO_PI; theta += theta_step) {
                    const float costheta = fast_cos(theta);
                    const float sintheta = fast_sin(theta);
                    const float circlex_base = R2 + R1 * costheta;
                    const float circley = R1 * sintheta;

                    const float ct_cb = circlex_base * cosB;
                    const float ct_sb = circlex_base * sinB;
                    const float cy_ca = circley * cosA;
                    const float cy_sa = circley * sinA;

                    for (float phi = 0.0f; phi < TWO_PI; phi += phi_step) {
                        const float cosphi = fast_cos(phi);
                        const float sinphi = fast_sin(phi);

                        const float x = ct_cb * cosphi + ct_sb * sinA * sinphi - cy_ca * sinB;
                        const float y_val = ct_sb * cosphi - (circlex_base * sinA * sinphi) * cosB + cy_ca * cosB;
                        const float z = K2 + cosA * circlex_base * sinphi + cy_sa;

                        if (z > 0.0f) {
                            const float ooz = 1.0f / z;
                            const int xp = (int)((float)WIDTH * 0.5f + object_pos_x + K1 * ooz * x * 2.0f);
                            const int yp = (int)((float)HEIGHT * 0.5f + object_pos_y - K1 * ooz * y_val);

                            if (xp >= 0 && xp < WIDTH && yp >= 0 && yp < HEIGHT) {
                                if (ooz > zbuffer[yp * WIDTH + xp]) {
                                    const float nx = cosphi * costheta;
                                    const float ny = sinphi * costheta;
                                    const float nz = sintheta;

                                    const float rnx = nx * cosB - (ny * cosA - nz * sinA) * sinB;
                                    const float rny = nx * sinB + (ny * cosA - nz * sinA) * cosB;
                                    const float rnz = ny * sinA + nz * cosA;

                                    // ✨ Реалистичная модель освещения Фонга
                                    const float diffuse = rnx * lx + rny * ly + rnz * lz;
                                    const float rim = powf(1.0f - fmaxf(0.0f, rnz), 3.0f) * 0.55f;
                                    const float specular = powf(fmaxf(0.0f, diffuse), 16.0f) * 0.35f;

                                    float L = diffuse * light_intensity + rim + specular;
                                    L = L * 1.3f + 0.4f; // Ярче подсветка, больше контраст

                                    if (L > 0.0f) {
                                        zbuffer[yp * WIDTH + xp] = ooz;
                                        int lum_idx = (int)(L * 8.0f);
                                        lum_idx = lum_idx > 11 ? 11 : (lum_idx < 0 ? 0 : lum_idx);

                                        int color_idx = lum_idx % palette_size;

                                        const int idx = yp * WIDTH + xp;
                                        screenBuffer[idx].Char.AsciiChar = luminance_chars[lum_idx];
                                        screenBuffer[idx].Attributes = current_palette[color_idx];
                                    }
                                }
                            }
                        }
                    }
                }

                // ✨ Звезда вращающаяся вокруг бублика
                const float star_orbit_radius = R2 + R1 * 1.8f;
                const float star_angle = light_time * 0.45f;

                const float star_x = star_orbit_radius * fast_cos(star_angle);
                const float star_y = star_orbit_radius * fast_sin(star_angle) * 0.7f;
                const float star_z = K2 + star_orbit_radius * fast_sin(star_angle * 0.7f);

                if (star_z > 0.0f) {
                    const float star_ooz = 1.0f / star_z;
                    const int sx = (int)((float)WIDTH * 0.5f + object_pos_x + K1 * star_ooz * star_x * 2.0f);
                    const int sy = (int)((float)HEIGHT * 0.5f + object_pos_y - K1 * star_ooz * star_y);

                    if (sx >= 0 && sx < WIDTH && sy >= 0 && sy < HEIGHT) {
                        const int sidx = sy * WIDTH + sx;

                        char star_char;
                        WORD star_color;

                        if (star_z < K2) {
                            star_char = '*';
                            star_color = 0x0C; // Красный
                        } else if (star_z < K2 + 2.0f) {
                            star_char = '+';
                            star_color = 0x0E; // Жёлтый
                        } else {
                            star_char = '.';
                            star_color = 0x06; // Коричневый
                        }

                        // Рисуем только если звезда перед бубликом или место пустое
                        if (star_z < K2 || zbuffer[sidx] == 0.0f) {
                            screenBuffer[sidx].Char.AsciiChar = star_char;
                            screenBuffer[sidx].Attributes = star_color;
                        }
                    }
                }
            }
            break;
    }

    if (shape_id == 0) {
        // Обновление позиции бублика (DVD стиль отскок)
        donut_pos_x += donut_vel_x;
        donut_pos_y += donut_vel_y;

        // Отскок от левой и правой границы
        if (donut_pos_x > (float)WIDTH * 0.5f - DONUT_BORDER_MARGIN ||
            donut_pos_x < -(float)WIDTH * 0.5f + DONUT_BORDER_MARGIN) {
            donut_vel_x = -donut_vel_x;
        }

        // Отскок от верхней и нижней границы
        if (donut_pos_y > (float)HEIGHT * 0.5f - DONUT_BORDER_MARGIN ||
            donut_pos_y < -(float)HEIGHT * 0.5f + DONUT_BORDER_MARGIN) {
            donut_vel_y = -donut_vel_y;
        }
    }

    WriteConsoleOutput(hConsoleBuffer, screenBuffer, bufferSize, bufferCoord, &writeRegion);
    Py_RETURN_NONE;
}

static PyObject* donut_init_console(PyObject* self, PyObject* args) {
    if (init_console()) {
        Py_RETURN_TRUE;
    } else {
        Py_RETURN_FALSE;
    }
}

static PyObject* donut_cleanup_console(PyObject* self, PyObject* args) {
    cleanup_console();
    Py_RETURN_NONE;
}

static PyMethodDef DonutMethods[] = {
    {"init_console", donut_init_console, METH_NOARGS, "Initialize console"},
    {"cleanup_console", donut_cleanup_console, METH_NOARGS, "Cleanup console"},
    {"set_girl_model", donut_set_girl_model, METH_VARARGS, "Set dancing girl model from girl.py"},
    {"render_frame", donut_render_frame, METH_VARARGS, "Render frame (A, B, light_time, zoom, shape_id=0)"},
    {"set_palette", donut_set_palette, METH_VARARGS, "Set palette by index (0-5)"},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef donutmodule = {
    PyModuleDef_HEAD_INIT,
    "donut_renderer",
    "Ultra-fast donut renderer with palettes",
    -1,
    DonutMethods
};

PyMODINIT_FUNC PyInit_donut_renderer(void) {
    return PyModule_Create(&donutmodule);
}