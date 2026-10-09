<#
.SYNOPSIS
    配置并构建 CrossRemoteDesktop。

.DESCRIPTION
    从任意工作目录调用 CMake 完成配置和构建。默认使用 Debug 配置并构建默认目标；
    可指定构建配置、目标、并行度，并在构建成功后运行 CTest。
    公共长参数与 build.sh 使用相同的 kebab-case 名称；PowerShell 参数使用单横线，
    例如 build.sh 的 --run-tests 对应 build.ps1 的 -run-tests。

.NOTES
    本文件使用带 BOM 的 UTF-8 编码，兼容 Windows PowerShell 5.1 和 PowerShell 7。

.EXAMPLE
    pwsh -File ./build.ps1

.EXAMPLE
    pwsh -File ./build.ps1 -configuration Release -target CrossRemoteDesktop -jobs 8

.EXAMPLE
    pwsh -File ./build.ps1 -run-tests

.EXAMPLE
    pwsh -File ./build.ps1 -target test_passwordcrypto -run-tests -test-regex PasswordCryptoTest

.EXAMPLE
    pwsh -File ./build.ps1 -qt-root C:\Qt\6.9.3\msvc2022_64
#>
[CmdletBinding(PositionalBinding = $false)]
param(
    [Alias('c')]
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$configuration = 'Debug',

    [Alias('B', 'build-directory')]
    [string]$buildDirectory = 'build',

    [Alias('t')]
    [string[]]$target = @(),

    [Alias('j')]
    [ValidateRange(0, 1024)]
    [int]$jobs = 0,

    [Alias('run-tests')]
    [switch]$runTests,

    [Alias('test-label')]
    [string]$testLabel,

    [Alias('test-regex')]
    [string]$testRegex,

    [Alias('configure-only')]
    [switch]$configureOnly,

    [Alias('skip-configure')]
    [switch]$skipConfigure,

    [Alias('G')]
    [string]$generator,

    [Alias('qt-root')]
    [string]$qtRoot,

    [Alias('prefix-path')]
    [string[]]$prefixPath = @(),

    [Alias('cmake-argument')]
    [string[]]$cMakeArgument = @(),

    [Alias('h')]
    [switch]$help
)

$ErrorActionPreference = 'Stop'

function Show-Help {
    $helpText = @'
配置并构建 CrossRemoteDesktop（Windows）。

用法:
  ./build.ps1 [选项]

选项:
  -c, -configuration <配置>    Debug、Release、RelWithDebInfo 或 MinSizeRel
                              （默认：Debug）
  -B, -build-directory <目录> 构建目录；相对路径以项目根目录为基准
                              （默认：build）
  -t, -target <目标[]>        构建指定目标；多个值使用逗号分隔
  -j, -jobs <数量>            并行任务数，范围 0..1024；0 表示使用 CMake 默认值
                              （默认：0）
      -run-tests              构建成功后运行 CTest
      -test-label <标签>      仅运行指定标签的测试
      -test-regex <正则>      仅运行名称匹配正则的测试
      -configure-only         仅执行 CMake 配置
      -skip-configure         跳过配置，直接使用已有构建目录
  -G, -generator <生成器>     指定 CMake 生成器
      -qt-root <目录>         Qt SDK 根目录；相对路径以调用目录为基准
      -prefix-path <目录[]>   额外 CMAKE_PREFIX_PATH；多个值使用逗号分隔
      -cmake-argument <参数[]> 额外 CMake 配置参数；多个值使用逗号分隔
  -h, -help                   显示帮助

示例:
  ./build.ps1
  ./build.ps1 -configuration Release -target CrossRemoteDesktop -jobs 8
  ./build.ps1 -run-tests
  ./build.ps1 -target test_passwordcrypto -run-tests -test-regex PasswordCryptoTest
  ./build.ps1 -qt-root C:\Qt\6.9.3\msvc2022_64
  ./build.ps1 -cmake-argument '-DENABLE_SANITIZERS=address'

说明:
  PowerShell 参数不区分大小写，旧的驼峰参数名继续兼容。
  多值参数的逗号数组写法适用于 PowerShell 会话；从其他 Shell 使用 -File 调用时，
  每个数组参数建议只传一个值。
'@
    Write-Host $helpText
}

function ConvertTo-AbsolutePath {
    param(
        [Parameter(Mandatory)]
        [string]$path,

        [Parameter(Mandatory)]
        [string]$baseDirectory
    )

    if ([System.IO.Path]::IsPathRooted($path)) {
        return [System.IO.Path]::GetFullPath($path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $baseDirectory $path))
}

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

if ($help) {
    Show-Help
    return
}

$configuration = switch ($configuration.ToLowerInvariant()) {
    'debug' { 'Debug' }
    'release' { 'Release' }
    'relwithdebinfo' { 'RelWithDebInfo' }
    'minsizerel' { 'MinSizeRel' }
}

if ($configureOnly -and $skipConfigure) {
    throw '-configure-only 与 -skip-configure 不能同时使用。'
}
if ($configureOnly -and $runTests) {
    throw '-configure-only 与 -run-tests 不能同时使用。'
}
if ([string]::IsNullOrWhiteSpace($buildDirectory)) {
    throw '-build-directory 不能为空。'
}
foreach ($buildTarget in $target) {
    if ([string]::IsNullOrWhiteSpace($buildTarget)) {
        throw '-target 不能为空。'
    }
    if ($buildTarget.EndsWith('\') -or $buildTarget.EndsWith('/')) {
        throw "目标名不能以斜杠结尾：'$buildTarget'。正确示例：-target CrossRemoteDesktop"
    }
}
foreach ($argument in $cMakeArgument) {
    if ([string]::IsNullOrWhiteSpace($argument)) {
        throw '-cmake-argument 不能为空。'
    }
}

$invocationDirectory = [System.IO.Path]::GetFullPath((Get-Location).Path)
$projectRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
$buildPath = ConvertTo-AbsolutePath -path $buildDirectory -baseDirectory $projectRoot

$cmake = (Get-Command cmake -ErrorAction Stop).Source
$ctest = if ($runTests) { (Get-Command ctest -ErrorAction Stop).Source } else { $null }

$resolvedPrefixPaths = @()
if (-not [string]::IsNullOrWhiteSpace($qtRoot)) {
    $resolvedPrefixPaths += ConvertTo-AbsolutePath -path $qtRoot -baseDirectory $invocationDirectory
}
foreach ($additionalPrefixPath in $prefixPath) {
    if ([string]::IsNullOrWhiteSpace($additionalPrefixPath)) {
        throw '-prefix-path 不能为空。'
    }
    $resolvedPrefixPaths += ConvertTo-AbsolutePath -path $additionalPrefixPath -baseDirectory $invocationDirectory
}
$prefixPathValue = $resolvedPrefixPaths -join ';'

Write-Host 'CrossRemoteDesktop 构建' -ForegroundColor Green
Write-Host "  源码目录: $projectRoot"
Write-Host "  构建目录: $buildPath"
Write-Host "  构建配置: $configuration"
if (-not [string]::IsNullOrWhiteSpace($prefixPathValue)) {
    Write-Host "  CMake 前缀: $prefixPathValue"
}

if (-not $skipConfigure) {
    $configureArguments = @(
        '-S', $projectRoot,
        '-B', $buildPath,
        "-DCMAKE_BUILD_TYPE=$configuration"
    )

    if (-not [string]::IsNullOrWhiteSpace($generator)) {
        $configureArguments += @('-G', $generator)
    }
    if (-not [string]::IsNullOrWhiteSpace($prefixPathValue)) {
        $configureArguments += "-DCMAKE_PREFIX_PATH=$prefixPathValue"
    }
    if ($cMakeArgument.Count -gt 0) {
        $configureArguments += $cMakeArgument
    }

    Invoke-CheckedCommand -command $cmake -argumentList $configureArguments
} elseif (-not (Test-Path -LiteralPath (Join-Path $buildPath 'CMakeCache.txt'))) {
    throw "指定了 -skip-configure，但构建目录中不存在 CMakeCache.txt：$buildPath"
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
        Write-Warning '指定 -target 时只构建所选目标；请确保筛选出的测试可执行文件已经构建。'
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
