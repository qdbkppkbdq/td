cmake_minimum_required(VERSION 3.28)
project(td)

# 设置项目名称
project(td)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加 include 目录
include_directories(${PROJECT_SOURCE_DIR}/include)

# 添加 src 目录下的所有源文件
file(GLOB SOURCES "src/*.cpp")

# 添加 main.cpp
list(APPEND SOURCES "main.cpp")

# 添加可执行文件
add_executable(${PROJECT_NAME} ${SOURCES})

# 如果有外部库，添加 lib 目录
link_directories(${PROJECT_SOURCE_DIR}/lib)
# 如果需要链接外部库，使用以下命令（替换 YourLibrary 为实际的库名）
# target_link_libraries(${PROJECT_NAME} YourLibrary)

# 添加测试（如果需要）
# enable_testing()
# add_subdirectory(test)