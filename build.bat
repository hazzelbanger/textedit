@echo off
setlocal

set APP_NAME=textedit
set SRC_DIR=src
set BUILD_DIR=build
set FREETYPE_DIR=C:\libraries\freetype

if not exist %BUILD_DIR% mkdir %BUILD_DIR%

echo Compiling %APP_NAME%...

cl /nologo /W3 /O2 ^
   /I%FREETYPE_DIR%\include ^
   /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN ^
   /Fe%BUILD_DIR%\%APP_NAME%.exe ^
   %SRC_DIR%\main.c %SRC_DIR%\text_buffer.c %SRC_DIR%\renderer.c ^
   /link ^
   user32.lib gdi32.lib kernel32.lib ^
   %FREETYPE_DIR%\lib\freetype.lib

if %ERRORLEVEL% EQU 0 (
    echo.
    echo Build successful: %BUILD_DIR%\%APP_NAME%.exe
    echo.
    echo NOTE: Make sure freetype.dll is in your PATH or next to the .exe
) else (
    echo.
    echo Build failed with error code %ERRORLEVEL%
)

endlocal
