from setuptools import setup, Extension
import os
import shutil

# Удаляем старые скомпилированные файлы
for root, dirs, files in os.walk('.'):
    for f in files:
        if f.endswith(('.pyd', '.so', '.dll')) and 'donut_renderer' in f:
            path = os.path.join(root, f)
            print(f"Removing old: {path}")
            os.remove(path)

# Удаляем папку build
if os.path.exists('build'):
    shutil.rmtree('build')

module = Extension(
    'donut_c.donut_renderer',
    sources=['donut_c/donut_renderer.c'],
    extra_compile_args=[
        '/O2', '/Oi', '/Ot', '/Oy', '/GL',
        '/arch:SSE2', '/fp:fast', '/W3',
    ],
    extra_link_args=['/LTCG'],
    libraries=['user32', 'kernel32']
)

setup(
    name='donut_renderer',
    version='3.0',
    description='Ultra-fast donut renderer',
    ext_modules=[module],
)