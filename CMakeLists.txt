cmake_minimum_required(VERSION 3.16)

project(OpenXidon VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(HIDAPI IMPORTED_TARGET hidapi-hidraw)
if(NOT HIDAPI_FOUND)
    pkg_check_modules(HIDAPI IMPORTED_TARGET hidapi-libusb)
endif()
if(NOT HIDAPI_FOUND)
    pkg_check_modules(HIDAPI REQUIRED IMPORTED_TARGET hidapi)
endif()

add_library(openxidon STATIC
    src/buttons.cpp
    src/dpi.cpp
    src/dry_run_transport.cpp
    src/general_block.cpp
    src/hid_transport.cpp
    src/keys.cpp
    src/lighting.cpp
    src/macro.cpp
    src/mouse.cpp
    src/util.cpp
)
target_include_directories(openxidon PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(openxidon PUBLIC PkgConfig::HIDAPI)

add_executable(openxidon-cli src/cli/main.cpp)
set_target_properties(openxidon-cli PROPERTIES OUTPUT_NAME openxidon)
target_link_libraries(openxidon-cli PRIVATE openxidon)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(openxidon PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(openxidon-cli PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(GNUInstallDirs)
install(TARGETS openxidon-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES udev/70-openxidon.rules DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/udev/rules.d)
