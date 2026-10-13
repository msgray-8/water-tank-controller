cmake_minimum_required(VERSION 3.20)
project(water_tank_controller VERSION 0.2.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(ENABLE_SANITIZERS "Enable host address/undefined-behavior checks" OFF)

add_library(project_options INTERFACE)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(project_options INTERFACE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Werror)
    if(ENABLE_SANITIZERS)
        target_compile_options(project_options INTERFACE
            -fsanitize=address,undefined -fno-sanitize-recover=all
            -fno-omit-frame-pointer)
        target_link_options(project_options INTERFACE -fsanitize=address,undefined)
    endif()
elseif(MSVC)
    target_compile_options(project_options INTERFACE /W4 /WX)
endif()

add_library(tank_controller src/tank_controller.c)
target_include_directories(tank_controller PUBLIC include)
target_link_libraries(tank_controller PRIVATE project_options)

add_executable(tank_demo src/main.c)
target_link_libraries(tank_demo PRIVATE tank_controller project_options)

include(CTest)
if(BUILD_TESTING)
    add_executable(test_tank_controller tests/test_tank_controller.c)
    target_link_libraries(test_tank_controller PRIVATE tank_controller project_options)
    add_test(NAME tank_controller_tests COMMAND test_tank_controller)
    add_test(NAME tank_demo_runs COMMAND tank_demo)
endif()
