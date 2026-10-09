#!/usr/bin/env bash

set -Eeo pipefail

# macOS 自带的 Bash 3.2 会在 nounset 模式下把空数组视为未定义。
# Bash 4.4 起已修复该行为；新版本继续启用 nounset 检查。
if ((BASH_VERSINFO[0] > 4 || (BASH_VERSINFO[0] == 4 && BASH_VERSINFO[1] >= 4))); then
    set -u
fi

show_help() {
    cat <<'EOF'
配置并构建 CrossRemoteDesktop（Linux / macOS 原生架构）。

用法:
  ./build.sh [选项] [-- <额外 CMake 配置参数>...]

选项:
  -c, --configuration <配置>    Debug、Release、RelWithDebInfo 或 MinSizeRel
                              （默认：Debug）
  -B, --build-directory <目录> 构建目录；相对路径以项目根目录为基准
                              （默认：build）
  -t, --target <目标>          构建指定目标；可重复使用
  -j, --jobs <数量>            并行任务数，范围 0..1024；0 表示使用 CMake 默认值
                              （默认：0）
      --run-tests              构建成功后运行 CTest
      --test-label <标签>      仅运行指定标签的测试
      --test-regex <正则>      仅运行名称匹配正则的测试
      --configure-only         仅执行 CMake 配置
      --skip-configure         跳过配置，直接使用已有构建目录
  -G, --generator <生成器>     指定 CMake 生成器
      --qt-root <目录>         Qt SDK 根目录；相对路径以调用目录为基准
      --prefix-path <目录>     额外 CMAKE_PREFIX_PATH；可重复，相对路径以调用目录为基准
      --cmake-argument <参数>  额外 CMake 配置参数；可重复使用
  -h, --help                   显示帮助

示例:
  ./build.sh
  ./build.sh --configuration Release --target CrossRemoteDesktop --jobs 8
  ./build.sh --run-tests
  ./build.sh --target test_passwordcrypto --run-tests --test-regex PasswordCryptoTest
  # Linux Qt SDK
  ./build.sh --qt-root "$HOME/Qt/6.9.3/gcc_64"
  ./build.sh --qt-root "$HOME/Qt/6.9.3/gcc_64" \
    --prefix-path "$HOME/.local/share/crossrd-linux-sdk/usr"
  # macOS Qt SDK
  ./build.sh --qt-root "$HOME/Qt/6.9.3/macos"
  ./build.sh --cmake-argument '-DENABLE_SANITIZERS=address'

说明:
  macOS 当前仅支持宿主机原生单架构，并需要先补齐对应架构的 third_party 缓存。
EOF
}

die() {
    printf '错误：%s\n' "$*" >&2
    exit 2
}

is_script_option() {
    case "$1" in
        --*|-c|-B|-t|-j|-G|-h) return 0 ;;
        *) return 1 ;;
    esac
}

is_blank() {
    [[ -z "${1//[[:space:]]/}" ]]
}

require_value() {
    local option=$1
    local value=${2-}
    if [[ -z "$value" ]] || is_script_option "$value"; then
        die "选项 ${option} 缺少参数；以 -- 开头的值请使用 ${option}=<值>。"
    fi
}

resolve_path() {
    local path=$1
    local base_directory=$2
    if [[ "$path" == /* ]]; then
        printf '%s\n' "$path"
    else
        printf '%s\n' "${base_directory}/${path}"
    fi
}

run_command() {
    printf '\n>'
    printf ' %q' "$@"
    printf '\n'
    "$@"
}

configuration='Debug'
build_directory='build'
jobs='0'
run_tests=false
configure_only=false
skip_configure=false
generator=''
qt_root=''
test_label=''
test_regex=''
targets=()
prefix_paths=()
cmake_arguments=()

while (($# > 0)); do
    case "$1" in
        -c|--configuration)
            require_value "$1" "${2-}"
            configuration=$2
            shift 2
            ;;
        --configuration=*)
            configuration=${1#*=}
            shift
            ;;
        -B|--build-directory)
            require_value "$1" "${2-}"
            build_directory=$2
            shift 2
            ;;
        --build-directory=*)
            build_directory=${1#*=}
            shift
            ;;
        -t|--target)
            require_value "$1" "${2-}"
            targets+=("$2")
            shift 2
            ;;
        --target=*)
            targets+=("${1#*=}")
            shift
            ;;
        -j|--jobs)
            require_value "$1" "${2-}"
            jobs=$2
            shift 2
            ;;
        --jobs=*)
            jobs=${1#*=}
            shift
            ;;
        --run-tests)
            run_tests=true
            shift
            ;;
        --test-label)
            require_value "$1" "${2-}"
            test_label=$2
            shift 2
            ;;
        --test-label=*)
            test_label=${1#*=}
            shift
            ;;
        --test-regex)
            require_value "$1" "${2-}"
            test_regex=$2
            shift 2
            ;;
        --test-regex=*)
            test_regex=${1#*=}
            shift
            ;;
        --configure-only)
            configure_only=true
            shift
            ;;
        --skip-configure)
            skip_configure=true
            shift
            ;;
        -G|--generator)
            require_value "$1" "${2-}"
            generator=$2
            shift 2
            ;;
        --generator=*)
            generator=${1#*=}
            shift
            ;;
        --qt-root)
            require_value "$1" "${2-}"
            qt_root=$2
            shift 2
            ;;
        --qt-root=*)
            qt_root=${1#*=}
            shift
            ;;
        --prefix-path)
            require_value "$1" "${2-}"
            prefix_paths+=("$2")
            shift 2
            ;;
        --prefix-path=*)
            prefix_paths+=("${1#*=}")
            shift
            ;;
        --cmake-argument)
            require_value "$1" "${2-}"
            cmake_arguments+=("$2")
            shift 2
            ;;
        --cmake-argument=*)
            cmake_arguments+=("${1#*=}")
            shift
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        --)
            shift
            while (($# > 0)); do
                cmake_arguments+=("$1")
                shift
            done
            ;;
        *)
            die "未知参数：$1。使用 --help 查看用法。"
            ;;
    esac
done

case "$configuration" in
    Debug|Release|RelWithDebInfo|MinSizeRel) ;;
    *) die "无效构建配置：${configuration}。" ;;
esac

if is_blank "$build_directory"; then
    die '构建目录不能为空。'
fi
jobs_input=$jobs
if [[ ! "$jobs" =~ ^[0-9]+$ ]]; then
    die "并行任务数必须是 0..1024 的整数：${jobs}。"
fi
jobs=${jobs#"${jobs%%[!0]*}"}
if [[ -z "$jobs" ]]; then
    jobs=0
fi
if ((${#jobs} > 4)); then
    die "并行任务数必须是 0..1024 的整数：${jobs_input}。"
fi
if ((${#jobs} == 4)) && [[ "$jobs" > '1024' ]]; then
    die "并行任务数必须是 0..1024 的整数：${jobs_input}。"
fi
if [[ "$configure_only" == true && "$skip_configure" == true ]]; then
    die '--configure-only 与 --skip-configure 不能同时使用。'
fi
if [[ "$configure_only" == true && "$run_tests" == true ]]; then
    die '--configure-only 与 --run-tests 不能同时使用。'
fi
for target in "${targets[@]}"; do
    if is_blank "$target"; then
        die '目标名不能为空。'
    fi
    if [[ "$target" == */ || "$target" == *\\ ]]; then
        die "目标名不能以斜杠结尾：${target}。"
    fi
done
for cmake_argument in "${cmake_arguments[@]}"; do
    if is_blank "$cmake_argument"; then
        die '--cmake-argument 不能为空。'
    fi
done

case "$(uname -s)" in
    Linux) platform_name='Linux' ;;
    Darwin) platform_name='macOS' ;;
    *) die 'build.sh 仅支持 Linux 和 macOS；Windows 请使用 build.ps1。' ;;
esac

invocation_directory=$(pwd -P)
script_directory=$(cd -- "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
readonly invocation_directory script_directory platform_name
readonly project_root=$script_directory
build_path=$(resolve_path "$build_directory" "$project_root")

cmake_command=$(command -v cmake) || die '未找到 cmake，请先安装 CMake 3.21+。'
ctest_command=''
if [[ "$run_tests" == true ]]; then
    ctest_command=$(command -v ctest) || die '未找到 ctest，请先安装 CMake/CTest。'
fi

resolved_prefix_paths=()
if ! is_blank "$qt_root"; then
    resolved_prefix_paths+=("$(resolve_path "$qt_root" "$invocation_directory")")
fi
for prefix_path in "${prefix_paths[@]}"; do
    if is_blank "$prefix_path"; then
        die 'CMAKE_PREFIX_PATH 目录不能为空。'
    fi
    resolved_prefix_paths+=("$(resolve_path "$prefix_path" "$invocation_directory")")
done

prefix_path_value=''
if ((${#resolved_prefix_paths[@]} > 0)); then
    prefix_path_value=$(IFS=';'; printf '%s' "${resolved_prefix_paths[*]}")
fi

printf 'CrossRemoteDesktop %s 构建\n' "$platform_name"
printf '  源码目录: %s\n' "$project_root"
printf '  构建目录: %s\n' "$build_path"
printf '  构建配置: %s\n' "$configuration"
if [[ -n "$prefix_path_value" ]]; then
    printf '  CMake 前缀: %s\n' "$prefix_path_value"
fi

if [[ "$skip_configure" != true ]]; then
    configure_arguments=(
        -S "$project_root"
        -B "$build_path"
        "-DCMAKE_BUILD_TYPE=${configuration}"
    )
    if ! is_blank "$generator"; then
        configure_arguments+=(-G "$generator")
    fi
    if [[ -n "$prefix_path_value" ]]; then
        configure_arguments+=("-DCMAKE_PREFIX_PATH=${prefix_path_value}")
    fi
    configure_arguments+=("${cmake_arguments[@]}")

    run_command "$cmake_command" "${configure_arguments[@]}"
elif [[ ! -f "${build_path}/CMakeCache.txt" ]]; then
    die "指定了 --skip-configure，但构建目录中不存在 CMakeCache.txt：${build_path}"
fi

if [[ "$configure_only" == true ]]; then
    printf '\nCMake 配置完成。\n'
    exit 0
fi

build_arguments=(
    --build "$build_path"
    --config "$configuration"
)
if ((${#targets[@]} > 0)); then
    build_arguments+=(--target "${targets[@]}")
fi
if ((jobs > 0)); then
    build_arguments+=(--parallel "$jobs")
fi

run_command "$cmake_command" "${build_arguments[@]}"

if [[ "$run_tests" == true ]]; then
    if ((${#targets[@]} > 0)); then
        printf '警告：指定 --target 时只构建所选目标；请确保筛选出的测试可执行文件已经构建。\n' >&2
    fi

    test_arguments=(
        -C "$configuration"
        --output-on-failure
        --no-tests=error
    )
    if ! is_blank "$test_label"; then
        test_arguments+=(-L "$test_label")
    fi
    if ! is_blank "$test_regex"; then
        test_arguments+=(-R "$test_regex")
    fi

    (
        cd -- "$build_path"
        run_command "$ctest_command" "${test_arguments[@]}"
    )
fi

output_configuration='Release'
if [[ "$configuration" == 'Debug' ]]; then
    output_configuration='Debug'
fi
if [[ "$platform_name" == 'macOS' ]]; then
    application_path="${project_root}/${output_configuration}/UltraDesktop.app/Contents/MacOS/UltraDesktop"
else
    application_path="${project_root}/${output_configuration}/UltraDesktop"
fi
built_application=false
if ((${#targets[@]} == 0)); then
    built_application=true
else
    for target in "${targets[@]}"; do
        if [[ "$target" == 'CrossRemoteDesktop' ]]; then
            built_application=true
            break
        fi
    done
fi

printf '\n构建成功。\n'
if [[ "$built_application" == true && -f "$application_path" ]]; then
    printf '  应用程序: %s\n' "$application_path"
fi
if [[ "$run_tests" == true ]]; then
    printf '  测试结果: 全部通过\n'
fi
