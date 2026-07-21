@echo off
:: setup-symlinks.bat
:: 在新设备上建立 Claude Code 所需的 symlink，使 memory/ 和 hooks/ 跟随 git 仓库跨设备同步
:: 用法：在项目根目录运行此脚本

setlocal enabledelayedexpansion

:: 获取当前目录（项目根目录，strip 尾部反斜杠）
set "PROJECT_DIR=%~dp0"
set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"

echo ========================================
echo   CrossRemoteDesktop - Symlink Setup
echo ========================================

:: ===========================================================================
:: 1. Memory symlink: ~\.claude\projects\<hash>\memory → <project>\memory
:: ===========================================================================
set "CLAUDE_PROJECTS=%USERPROFILE%\.claude\projects"
set "FOUND=0"

for /d %%d in ("%CLAUDE_PROJECTS%\*CrossRemoteDesktop*") do (
    set "MEMORY_LINK=%%d\memory"
    if exist "%%d\" (
        echo.
        echo [memory] Found Claude project dir: %%d
        set "FOUND=1"

        if exist "!MEMORY_LINK!" (
            if exist "!MEMORY_LINK!\MEMORY.md" (
                echo [memory] Symlink already exists, skipping.
            ) else (
                echo [memory] Removing stale directory...
                rmdir /s /q "!MEMORY_LINK!" 2>nul
                goto :create_memory_junction
            )
        ) else (
            :create_memory_junction
            mklink /J "!MEMORY_LINK!" "%PROJECT_DIR%\memory" >nul 2>&1
            if !errorlevel! equ 0 (
                echo [memory] [OK] Junction created: !MEMORY_LINK! -^> %PROJECT_DIR%\memory
            ) else (
                echo [memory] [FAIL] Could not create junction.
            )
        )
    )
)

if "%FOUND%"=="0" (
    echo [memory] [SKIP] Claude project directory not found under %CLAUDE_PROJECTS%
    echo        Run Claude Code in this project at least once first.
)

:: ===========================================================================
:: 2. Hooks symlink: .claude\hooks → ..\hooks
:: ===========================================================================
set "LINK=%PROJECT_DIR%\.claude\hooks"
set "TARGET=%PROJECT_DIR%\hooks"

echo.
if exist "%LINK%" (
    dir /al "%LINK%" >nul 2>&1
    if !errorlevel! equ 0 (
        echo [hooks] Symlink already exists, skipping.
    ) else (
        echo [hooks] Existing directory found (not a symlink), skipping.
    )
) else (
    mklink /J "%LINK%" "%TARGET%" >nul 2>&1
    if !errorlevel! equ 0 (
        echo [hooks] [OK] Junction created: .claude\hooks -^> hooks\
    ) else (
        echo [hooks] [FAIL] Could not create junction.
    )
)

:: ===========================================================================
:: 3. Skills symlink: .claude\skills → ..\skills
:: ===========================================================================
set "LINK=%PROJECT_DIR%\.claude\skills"
set "TARGET=%PROJECT_DIR%\skills"

echo.
if exist "%LINK%" (
    dir /al "%LINK%" >nul 2>&1
    if !errorlevel! equ 0 (
        echo [skills] Symlink already exists, skipping.
    ) else (
        echo [skills] Existing directory found (not a symlink), skipping.
    )
) else (
    mklink /J "%LINK%" "%TARGET%" >nul 2>&1
    if !errorlevel! equ 0 (
        echo [skills] [OK] Junction created: .claude\skills -^> skills\
    ) else (
        echo [skills] [FAIL] Could not create junction.
    )
)

echo.
echo ========================================
echo   Setup complete.
echo ========================================
endlocal
