@echo off
:: setup-memory-symlink.bat
:: 在新设备上建立 Claude 项目记忆 symlink，使记忆跟随 git 仓库跨设备同步
:: 用法：在项目根目录运行此脚本

setlocal enabledelayedexpansion

:: 获取当前目录（项目根目录）
set "PROJECT_DIR=%~dp0"
set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"

:: 构建 Claude 记忆存储路径
set "CLAUDE_PROJECTS=%USERPROFILE%\.claude\projects"

:: 用项目路径的 hash 变体匹配目录名（Hash 基于路径生成）
:: 尝试查找包含 CrossRemoteDesktop 的目录
set "FOUND=0"
for /d %%d in ("%CLAUDE_PROJECTS%\*CrossRemoteDesktop*") do (
    set "MEMORY_LINK=%%d\memory"
    if exist "%%d\" (
        echo Found Claude project dir: %%d
        set "FOUND=1"

        :: 如果已存在 memory 目录，备份后删除
        if exist "!MEMORY_LINK!" (
            if not exist "!MEMORY_LINK!\MEMORY.md" (
                echo Removing old memory directory...
                rmdir /s /q "!MEMORY_LINK!" 2>nul
            ) else (
                echo Memory symlink already exists, skipping.
                goto :done
            )
        )

        :: 创建 Junction
        mklink /J "!MEMORY_LINK!" "%PROJECT_DIR%\.claude\memory"
        if !errorlevel! equ 0 (
            echo [OK] Memory symlink created: !MEMORY_LINK! -^> %PROJECT_DIR%\.claude\memory
        ) else (
            echo [FAIL] Could not create symlink. Run as Administrator?
        )
    )
)

if "%FOUND%"=="0" (
    echo Claude project directory not found under %CLAUDE_PROJECTS%
    echo Please run Claude Code in this project at least once first.
)

:done
endlocal
