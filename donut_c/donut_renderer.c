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

static float sin_table[TABLE_SIZE];
static float cos_table[TABLE_SIZE];
static int tables_initialized = 0;

static WORD current_palette[MAX_PALETTE_SIZE];
static int palette_size = 7;

// DVD стиль движения бублика
static float donut_pos_x = 0.0f;
static float donut_pos_y = 0.0f;
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

static PyObject* donut_render_frame(PyObject* self, PyObject* args) {
    float A, B, light_time, zoom;
    
    if (!PyArg_ParseTuple(args, "ffff", &A, &B, &light_time, &zoom)) {
        return NULL;
    }
    
    if (!hConsoleBuffer) {
        PyErr_SetString(PyExc_RuntimeError, "Console not initialized");
        return NULL;
    }
    
    // Очистка буферов на весь размер
    memset(screenBuffer, 0, WIDTH * HEIGHT * sizeof(CHAR_INFO));
    memset(zbuffer, 0, WIDTH * HEIGHT * sizeof(float));
    
    const float cosA = fast_cos(A);
    const float sinA = fast_sin(A);
    const float cosB = fast_cos(B);
    const float sinB = fast_sin(B);
    const float K1 = (float)HEIGHT * K2 * 3.0f / (4.0f * (R1 + R2)) * zoom;
    
    bufferSize.X = WIDTH;
    bufferSize.Y = HEIGHT;
    writeRegion.Right = WIDTH - 1;
    writeRegion.Bottom = HEIGHT - 1;
    
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
                    const int xp = (int)((float)WIDTH * 0.5f + donut_pos_x + K1 * ooz * x * 2.0f);
                    const int yp = (int)((float)HEIGHT * 0.5f + donut_pos_y - K1 * ooz * y_val);
                
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
        const int sx = (int)((float)WIDTH * 0.5f + donut_pos_x + K1 * star_ooz * star_x * 2.0f);
        const int sy = (int)((float)HEIGHT * 0.5f + donut_pos_y - K1 * star_ooz * star_y);
        
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
    {"render_frame", donut_render_frame, METH_VARARGS, "Render frame (A, B, light_time, zoom)"},
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