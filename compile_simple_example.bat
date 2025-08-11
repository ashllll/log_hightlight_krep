@echo off  
echo Compiling simple example with krep_simple.dll...
gcc examples/example_usage.c -I. -Iheaders -Lsimple -lkrep_simple -o example_usage.exe
if %errorlevel% equ 0 (
    echo ✓ Simple compilation successful! Run example_usage.exe  
) else (
    echo ✗ Simple compilation failed!
)
pause
