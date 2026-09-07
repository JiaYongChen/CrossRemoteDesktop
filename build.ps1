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
    pwsh -File ./build.ps1 -configuration Release -target CrossRemoteDesktop

.EXAMPLE
    pwsh -File ./build.ps1 -runTests

.EXAMPLE
    pwsh -File ./build.ps1 -target test_passwordcrypto -runTests -testRegex PasswordCryptoTest

.EXAMPLE
    pwsh -File ./build.ps1 -qtRoot C:\Qt\6.9.3\msvc2022_64
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$configuration = 'Debug',

    [string]$buildDirectory = 'build',

    [string[]]$target = @(),

    [ValidateRange(0, 1024)]
    [int]$jobs = 0,

    [switch]$runTests,

    [string]$testLabel,

    [string]$testRegex,

    [switch]$configureOnly,

    [switch]$skipConfigure,

    [string]$generator,

    [string]$qtRoot,

    [string[]]$cMakeArgument = @()
)

$ErrorActionPreference = 'Stop'

function Invoke-CheckedCommand {
    param(
        [Parameter(Mandatory)]
        [string]$command,

        [Parameter(Mandatory)]
        [string[]]$argumentList
    )

    Write-Host ("`n> {0} {1}" -f $command, ($argumentList -join ' ')) -ForegroundColor Cyan
    & $command @argumentList
    if ($LASTEXITCODE -ne 0) {
        throw "命令执行失败（退出码 ${LASTEXITCODE}）：$command"
    }
}

if ($configureOnly -and $skipConfigure) {
    throw '-configureOnly 与 -skipConfigure 不能同时使用。'
}
if ($configureOnly -and $runTests) {
    throw '-configureOnly 与 -runTests 不能同时使用。'
}
foreach ($buildTarget in $target) {
    if ($buildTarget.EndsWith('\') -or $buildTarget.EndsWith('/')) {
        throw "目标名不能以斜杠结尾：'$buildTarget'。正确示例：-target CrossRemoteDesktop"
    }
}

$projectRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
if ([System.IO.Path]::IsPathRooted($buildDirectory)) {
    $buildPath = [System.IO.Path]::GetFullPath($buildDirectory)
} else {
    $buildPath = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $buildDirectory))
}

$cmake = (Get-Command cmake -ErrorAction Stop).Source
$ctest = if ($runTests) { (Get-Command ctest -ErrorAction Stop).Source } else { $null }

Write-Host 'CrossRemoteDesktop 构建' -ForegroundColor Green
Write-Host "  源码目录: $projectRoot"
Write-Host "  构建目录: $buildPath"
Write-Host "  构建配置: $configuration"

if (-not $skipConfigure) {
    $configureArguments = @(
        '-S', $projectRoot,
        '-B', $buildPath,
        "-DCMAKE_BUILD_TYPE=$configuration"
    )

    if (-not [string]::IsNullOrWhiteSpace($generator)) {
        $configureArguments += @('-G', $generator)
    }
    if (-not [string]::IsNullOrWhiteSpace($qtRoot)) {
        $qtPath = [System.IO.Path]::GetFullPath($qtRoot)
        $configureArguments += "-DCMAKE_PREFIX_PATH=$qtPath"
    }
    if ($cMakeArgument.Count -gt 0) {
        $configureArguments += $cMakeArgument
    }

    Invoke-CheckedCommand -command $cmake -argumentList $configureArguments
} elseif (-not (Test-Path -LiteralPath (Join-Path $buildPath 'CMakeCache.txt'))) {
    throw "指定了 -skipConfigure，但构建目录中不存在 CMakeCache.txt：$buildPath"
}

if ($configureOnly) {
    Write-Host "`nCMake 配置完成。" -ForegroundColor Green
    return
}

$buildArguments = @(
    '--build', $buildPath,
    '--config', $configuration
)
if ($target.Count -gt 0) {
    $buildArguments += '--target'
    $buildArguments += $target
}
if ($jobs -gt 0) {
    $buildArguments += @('--parallel', $jobs)
}

Invoke-CheckedCommand -command $cmake -argumentList $buildArguments

if ($runTests) {
    if ($target.Count -gt 0) {
        Write-Warning '指定 -target 时只会构建所选目标；请确保与筛选条件匹配的测试可执行文件已经构建。'
    }

    $testArguments = @(
        '--test-dir', $buildPath,
        '-C', $configuration,
        '--output-on-failure',
        '--no-tests=error'
    )
    if (-not [string]::IsNullOrWhiteSpace($testLabel)) {
        $testArguments += @('-L', $testLabel)
    }
    if (-not [string]::IsNullOrWhiteSpace($testRegex)) {
        $testArguments += @('-R', $testRegex)
    }

    Invoke-CheckedCommand -command $ctest -argumentList $testArguments
}

$outputConfiguration = if ($configuration -eq 'Debug') { 'Debug' } else { 'Release' }
$applicationPath = Join-Path $projectRoot "$outputConfiguration\UltraDesktop.exe"
$builtApplication = $target.Count -eq 0 -or $target -contains 'CrossRemoteDesktop'

Write-Host "`n构建成功。" -ForegroundColor Green
if ($builtApplication -and (Test-Path -LiteralPath $applicationPath)) {
    Write-Host "  应用程序: $applicationPath"
}
if ($runTests) {
    Write-Host '  测试结果: 全部通过'
}
