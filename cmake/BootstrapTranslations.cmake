# cmake/BootstrapTranslations.cmake
# 由 release_translations 目标在每次编译前通过 cmake -P 调用。
# 仅当 .ts 缺失时（全新 clone / 构建目录被清理）运行 lupdate 生成 .ts 骨架，
# 避免 lrelease 因找不到输入 .ts 而失败。已存在则跳过（保留已有译文）。
#
# 必需定义变量：-DTRANSLATIONS_OUT_DIR=... -DLUPDATE_EXECUTABLE=...
#   -DTS_ZH=... -DTS_EN=... -DSOURCE_DIR=... -DSOURCES_FILE=...
# 源码列表经 SOURCES_FILE（每行一个路径）传递，避免 -D 传分号列表被截断。

if(NOT EXISTS "${TRANSLATIONS_OUT_DIR}/zh_CN.ts")
    file(STRINGS "${SOURCES_FILE}" _sources)
    list(LENGTH _sources _sources_len)
    message(STATUS "[Translation] .ts missing — bootstrapping via lupdate (${_sources_len} sources)")
    execute_process(
        COMMAND ${LUPDATE_EXECUTABLE} ${_sources} -ts ${TS_ZH} ${TS_EN} -no-obsolete
        WORKING_DIRECTORY ${SOURCE_DIR}
        RESULT_VARIABLE _lupdate_result
    )
    if(NOT _lupdate_result EQUAL 0)
        message(WARNING "[Translation] lupdate bootstrap failed (code ${_lupdate_result})")
    endif()
endif()
