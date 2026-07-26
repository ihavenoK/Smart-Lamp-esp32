# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

# 引入 ESP-IDF 项目构建脚本
# IDF_PATH 环境变量由 VSCode ESP-IDF 插件自动设置
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# 告诉 CMake 在 components/ 目录下找自定义组件
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components"
)

project(Smart-Lamp)
