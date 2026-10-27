# The Incredible Hulk: Ultimate Destruction (original Xbox), statically recompiled

cmake_minimum_required(VERSION 3.25)
project(HulkRecompiled LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# the SDK: sdk/ here, two folders up inside the SDK's templates, a checkout beside this repository, else downloaded
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/sdk/cmake/XbeRecomp.cmake)
    set(SDK ${CMAKE_CURRENT_SOURCE_DIR}/sdk)
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/XbeRecomp.cmake)
    set(SDK ${CMAKE_CURRENT_SOURCE_DIR}/../..)
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../XboxXBE-Recomp/cmake/XbeRecomp.cmake)
    set(SDK ${CMAKE_CURRENT_SOURCE_DIR}/../XboxXBE-Recomp)
else()
    include(FetchContent)
    FetchContent_Declare(xbe_sdk GIT_REPOSITORY https://github.com/TGP482/XboxXBE-Recomp.git GIT_TAG main)
    FetchContent_MakeAvailable(xbe_sdk)
    set(SDK ${xbe_sdk_SOURCE_DIR})
endif()
include(${SDK}/cmake/XbeRecomp.cmake)

add_executable(hulk_disasm tools/disasm.cpp)
target_include_directories(hulk_disasm PRIVATE ${SDK}/runtime)
target_link_libraries(hulk_disasm PRIVATE Zydis)

enable_testing()
add_executable(hulk_analysis_tests tests/analysis.cpp ${SDK}/tools/recomp/analysis.cpp)
target_include_directories(hulk_analysis_tests PRIVATE ${SDK}/runtime ${SDK}/tools/recomp)
target_link_libraries(hulk_analysis_tests PRIVATE Zydis)
add_test(NAME vtable_method_discovery COMMAND hulk_analysis_tests)
add_executable(hulk_input_tests tests/keyboard.cpp src/common/keyboard.cpp)
target_include_directories(hulk_input_tests PRIVATE src ${SDK}/runtime)
add_test(NAME keyboard_input_lifecycle COMMAND hulk_input_tests)
add_executable(hulk_kernel_io_tests tests/kernel_io.cpp ${SDK}/runtime/kernel/io_completion.cpp)
target_include_directories(hulk_kernel_io_tests PRIVATE ${SDK}/runtime)
add_test(NAME kernel_io_completion COMMAND hulk_kernel_io_tests)
add_executable(hulk_display_menu_tests tests/display_menu.cpp src/common/display_menu.cpp)
target_include_directories(hulk_display_menu_tests PRIVATE src ${SDK}/runtime)
add_test(NAME display_menu_rcf COMMAND hulk_display_menu_tests ${CMAKE_CURRENT_SOURCE_DIR}/assets/misc.rcf)

file(GLOB XBE_HOST_SOURCES CONFIGURE_DEPENDS src/*/*.cpp)
list(FILTER XBE_HOST_SOURCES EXCLUDE REGEX "/src/game(_usa)?/")

# partitions as xbe_symbols wrote them into config/game.toml
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS config/game.toml)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/config/game.toml)
    file(STRINGS config/game.toml GAME_PARTITIONS REGEX "^partitions *=")
    string(REGEX MATCH "[0-9]+" GAME_PARTITIONS "${GAME_PARTITIONS}")
endif()
# no config yet: build.bat writes it with xbe_symbols, the change reconfigures
if(NOT GAME_PARTITIONS)
    set(GAME_PARTITIONS 8)
endif()

# target name = GAME_EXE in build.bat, xbe = GAME_XBE
add_xbe_image(HulkRecompiled default.xbe game.toml ${GAME_PARTITIONS} game)
# The runtime rebuilds a DLL older than its host. Relink after host-only changes too,
# even when the linker leaves the unchanged export import-library timestamp alone.
set_property(TARGET HulkRecompiled_code APPEND PROPERTY LINK_DEPENDS $<TARGET_FILE:HulkRecompiled>)

# The North American release (USA, Asia) is another build of the game: its own config and hooks at its own
# addresses (src/common/addresses.h), built when that disc's default.xbe is in assets/usa
set(HULK_TARGETS HulkRecompiled)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/assets/usa/default.xbe AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/config/game_usa.toml)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS config/game_usa.toml)
    file(STRINGS config/game_usa.toml USA_PARTITIONS REGEX "^partitions *=")
    string(REGEX MATCH "[0-9]+" USA_PARTITIONS "${USA_PARTITIONS}")
    add_xbe_image(HulkRecompiledUSA usa/default.xbe game_usa.toml ${USA_PARTITIONS} game_usa)
    target_compile_definitions(HulkRecompiledUSA PRIVATE HULK_USA=1)
    set_property(TARGET HulkRecompiledUSA_code APPEND PROPERTY LINK_DEPENDS $<TARGET_FILE:HulkRecompiledUSA>)
    list(APPEND HULK_TARGETS HulkRecompiledUSA)
endif()

option(HULK_DIAGNOSTICS "Keep detailed runtime logs during port bring-up" ON)
if(HULK_DIAGNOSTICS)
    foreach(target ${HULK_TARGETS})
        target_compile_options(${target} PRIVATE -UNDEBUG)
    endforeach()
endif()
