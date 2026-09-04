<#
.SYNOPSIS
    配置并构建 CrossRemoteDesktop。

.DESCRIPTION
    从任意工作目录调用 CMake 完成配置和构建。默认使用 Debug 配置并构建默认目标；
    可指定构建配置、目标、并行度，并在构建成功后运行 CTest。

.NOTES
    本文件使用带 BOM 的 UTF-8 编码，兼容 Windows PowerShell 5.1 和 PowerShell 7。

.EXAMPLE
    pwsh -File ./build.ps1

.EXAMPLE
    pwsh -File ./build.ps1 -Configuration Release -Target CrossRemoteDesktop

.EXAMPLE
    pwsh -File ./build.ps1 -RunTests

.EXAMPLE
    pwsh -File ./build.ps1 -Target test_passwordcrypto -RunTests -TestRegex PasswordCryptoTest

.EXAMPLE
    pwsh -File ./build.ps1 -QtRoot C:\Qt\6.9.3\msvc2022_64
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Debug',

    [string]$BuildDirectory = 'build',

    [string[]]$Target = @(),

    [ValidateRange(0, 1024)]
    [int]$Jobs = 0,

    [switch]$RunTests,

    [string]$TestLabel,

    [string]$TestRegex,

    [switch]$ConfigureOnly,

    [switch]$SkipConfigure,

    [string]$Generator,

    [string]$QtRoot,

    [string[]]$CMakeArgument = @()
)

$ErrorActionPreference = 'Stop'

function Invoke-CheckedCommand {
    param(
        [Parameter(Mandatory)]
        [string]$Command,

        [Parameter(Mandatory)]
        [string[]]$ArgumentList
    )

    Write-Host ("`n> {0} {1}" -f $Command, ($ArgumentList -join ' ')) -ForegroundColor Cyan
    & $Command @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        throw "命令执行失败（退出码 ${LASTEXITCODE}）：$Command"
    }
}

if ($ConfigureOnly -and $SkipConfigure) {
    throw '-ConfigureOnly 与 -SkipConfigure 不能同时使用。'
}
if ($ConfigureOnly -and $RunTests) {
    throw '-ConfigureOnly 与 -RunTests 不能同时使用。'
}
foreach ($buildTarget in $Target) {
    if ($buildTarget.EndsWith('\') -or $buildTarget.EndsWith('/')) {
        throw "目标名不能以斜杠结尾：'$buildTarget'。正确示例：-Target CrossRemoteDesktop"
    }
}

$projectRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
if ([System.IO.Path]::IsPathRooted($BuildDirectory)) {
    $buildPath = [System.IO.Path]::GetFullPath($BuildDirectory)
} else {
    $buildPath = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDirectory))
}

$cmake = (Get-Command cmake -ErrorAction Stop).Source
$ctest = if ($RunTests) { (Get-Command ctest -ErrorAction Stop).Source } else { $null }

Write-Host 'CrossRemoteDesktop 构建' -ForegroundColor Green
Write-Host "  源码目录: $projectRoot"
Write-Host "  构建目录: $buildPath"
Write-Host "  构建配置: $Configuration"

if (-not $SkipConfigure) {
    $configureArguments = @(
        '-S', $projectRoot,
        '-B', $buildPath,
        "-DCMAKE_BUILD_TYPE=$Configuration"
    )

    if (-not [string]::IsNullOrWhiteSpace($Generator)) {
        $configureArguments += @('-G', $Generator)
    }
    if (-not [string]::IsNullOrWhiteSpace($QtRoot)) {
        $qtPath = [System.IO.Path]::GetFullPath($QtRoot)
        $configureArguments += "-DCMAKE_PREFIX_PATH=$qtPath"
    }
    if ($CMakeArgument.Count -gt 0) {
        $configureArguments += $CMakeArgument
    }

    Invoke-CheckedCommand -Command $cmake -ArgumentList $configureArguments
} elseif (-not (Test-Path -LiteralPath (Join-Path $buildPath 'CMakeCache.txt'))) {
    throw "指定了 -SkipConfigure，但构建目录中不存在 CMakeCache.txt：$buildPath"
}

if ($ConfigureOnly) {
    Write-Host "`nCMake 配置完成。" -ForegroundColor Green
    return
}

$buildArguments = @(
    '--build', $buildPath,
    '--config', $Configuration
)
if ($Target.Count -gt 0) {
    $buildArguments += '--target'
    $buildArguments += $Target
}
if ($Jobs -gt 0) {
    $buildArguments += @('--parallel', $Jobs)
}

Invoke-CheckedCommand -Command $cmake -ArgumentList $buildArguments

if ($RunTests) {
    if ($Target.Count -gt 0) {
        Write-Warning '指定 -Target 时只会构建所选目标；请确保与筛选条件匹配的测试可执行文件已经构建。'
    }

    $testArguments = @(
        '--test-dir', $buildPath,
        '-C', $Configuration,
        '--output-on-failure'
    )
    if (-not [string]::IsNullOrWhiteSpace($TestLabel)) {
        $testArguments += @('-L', $TestLabel)
    }
    if (-not [string]::IsNullOrWhiteSpace($TestRegex)) {
        $testArguments += @('-R', $TestRegex)
    }

    Invoke-CheckedCommand -Command $ctest -ArgumentList $testArguments
}

$outputConfiguration = if ($Configuration -eq 'Debug') { 'Debug' } else { 'Release' }
$applicationPath = Join-Path $projectRoot "$outputConfiguration\UltraDesktop.exe"
$builtApplication = $Target.Count -eq 0 -or $Target -contains 'CrossRemoteDesktop'

Write-Host "`n构建成功。" -ForegroundColor Green
if ($builtApplication -and (Test-Path -LiteralPath $applicationPath)) {
    Write-Host "  应用程序: $applicationPath"
}
if ($RunTests) {
    Write-Host '  测试结果: 全部通过'
}
