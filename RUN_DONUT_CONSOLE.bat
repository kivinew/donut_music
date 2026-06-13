@echo off

rem -------------------------------------------------
rem 1️⃣ Создать и активировать virtualenv (если ещё нет)
rem -------------------------------------------------
if not exist .venv (
    uv venv .venv
)
call .venv\Scripts\activate

rem -------------------------------------------------
rem 2️⃣ Добавить зависимости
rem -------------------------------------------------
uv add pyinstaller 
uv add -r requirements.txt 
uv sync  

rem 3️⃣ Сборка С-расширения 
uv pip install -e .

rem 4️⃣ Сборка исполняемого exe файла в папке
uv run -m PyInstaller --onefile --console main.py

echo -------------------------------------------------
echo Installation finished.
echo -------------------------------------------------

rem -------------------------------------------------
rem 5️⃣ Запуск 3D бублика
rem -------------------------------------------------
@REM uv run .\main.py
call .\dist\main.exe