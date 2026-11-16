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
    core/pif/pif.cpp
    core/pif/pif.h
    core/cpu/r4300i.cpp
    core/cpu/r4300i.h
    core/memory/n64_memory_bus.cpp
    core/memory/n64_memory_bus.h
    core/n64_system.cpp
    core/n64_system.h
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

include(CTest)

if(BUILD_TESTING)
    add_executable(r4300i-tests
        tests/r4300i_tests.cpp
        core/cpu/r4300i.cpp
        core/cpu/r4300i.h
    )

    target_include_directories(r4300i-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_compile_options(r4300i-tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    )

    add_test(NAME r4300i-cpu-tests COMMAND r4300i-tests)

    add_executable(memory-bus-tests
        tests/memory_bus_tests.cpp
        core/memory/n64_memory_bus.cpp
        core/pif/pif.cpp
        core/pif/pif.h
        core/cpu/r4300i.cpp
        core/memory/n64_memory_bus.h
        core/cpu/r4300i.h
    )

    target_include_directories(memory-bus-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_compile_options(memory-bus-tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    )

    add_test(NAME n64-memory-bus-tests COMMAND memory-bus-tests)

    add_executable(n64-system-tests
        tests/n64_system_tests.cpp
        core/n64_system.cpp
        core/n64_system.h
        core/memory/n64_memory_bus.cpp
        core/memory/n64_memory_bus.h
        core/cpu/r4300i.cpp
        core/cpu/r4300i.h
    )

    target_include_directories(n64-system-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_compile_options(n64-system-tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    )

    add_test(NAME n64-system-integration-tests COMMAND n64-system-tests)

    add_executable(rom-file-tests
        tests/rom_file_tests.cpp
        core/rom/rom_file.cpp
        core/rom/rom_file.h
    )

    target_include_directories(rom-file-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(rom-file-tests PRIVATE Qt6::Core)

    target_compile_options(rom-file-tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    )

    add_test(NAME n64-rom-normalization-tests COMMAND rom-file-tests)
endif()
