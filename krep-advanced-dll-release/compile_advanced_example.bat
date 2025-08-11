@echo off
echo Compiling advanced example with krep_advanced.dll...
gcc examples/test_advanced.c -I. -Iheaders -Ladvanced -lkrep_advanced -o test_advanced.exe
if %errorlevel% equ 0 (
    echo ✓ Advanced compilation successful! Run test_advanced.exe
) else (
    echo ✗ Advanced compilation failed!
)
pause
