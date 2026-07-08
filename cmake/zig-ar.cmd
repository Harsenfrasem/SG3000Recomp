@echo off
where python-zig >nul 2>nul
if %errorlevel% equ 0 (
    python-zig ar %*
) else (
    python -m ziglang ar %*
)
