@echo off
where python-zig >nul 2>nul
if %errorlevel% equ 0 (
    python-zig ranlib %*
) else (
    python -m ziglang ranlib %*
)
