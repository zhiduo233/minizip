cmake_minimum_required(VERSION 3.10)

# 1. 项目名称
project(minibackup)

# 2. 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 3. 包含头文件目录
include_directories(include)

# 4. 生成可执行文件
add_executable(minibackup
    src/main.cpp
    src/BackupEngine.cpp)