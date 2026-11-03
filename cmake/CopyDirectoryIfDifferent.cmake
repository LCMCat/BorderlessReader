# 递归复制目录，只复制内容发生变化的文件。
# 用法：cmake -DSRC=<源目录> -DDST=<目标目录> -P CopyDirectoryIfDifferent.cmake

if(NOT DEFINED SRC OR NOT DEFINED DST)
    message(FATAL_ERROR "SRC and DST are required")
endif()

file(GLOB_RECURSE _files RELATIVE "${SRC}" "${SRC}/*")

foreach(_relative ${_files})
    get_filename_component(_target_dir "${DST}/${_relative}" DIRECTORY)
    file(MAKE_DIRECTORY "${_target_dir}")
    file(COPY_FILE "${SRC}/${_relative}" "${DST}/${_relative}" ONLY_IF_DIFFERENT)
endforeach()
