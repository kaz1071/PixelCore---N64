cmake_minimum_required(VERSION 3.20)

project(PixelCoreN64
    VERSION 0.1.0
    DESCRIPTION "A Nintendo 64 emulator for Linux"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)

qt_add_executable(pixelcore-n64
    app/main.cpp
    app/main_window.cpp
    app/main_window.h
    core/rom/rom_file.cpp
    core/rom/rom_file.h
)

target_include_directories(pixelcore-n64 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(pixelcore-n64 PRIVATE Qt6::Widgets)

target_compile_options(pixelcore-n64 PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

install(TARGETS pixelcore-n64
    RUNTIME DESTINATION bin
)
