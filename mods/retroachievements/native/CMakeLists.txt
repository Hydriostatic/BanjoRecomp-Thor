# Native half of the RetroAchievements mod: bk_retroachievements.so (or .dll/.dylib), loaded by
# the mod loader next to bk_retroachievements.nrm.
#
# For the AYN Thor / Android:
#   cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
#         -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_static
cmake_minimum_required(VERSION 3.20)
project(bk_retroachievements LANGUAGES C CXX)

set(RA_MOD_VERSION "0.1.0" CACHE STRING "Mod version, sent in the user agent")
set(RCHEEVOS_TAG "v12.5.0" CACHE STRING "rcheevos release to build against")
set(N64RECOMP_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../../lib/N64ModernRuntime/N64Recomp/include"
    CACHE PATH "Folder holding N64Recomp's recomp.h")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

include(FetchContent)
FetchContent_Declare(rcheevos
    GIT_REPOSITORY https://github.com/RetroAchievements/rcheevos.git
    GIT_TAG ${RCHEEVOS_TAG}
    GIT_SHALLOW TRUE)
FetchContent_GetProperties(rcheevos)
if(NOT rcheevos_POPULATED)
    FetchContent_Populate(rcheevos)
endif()

file(GLOB_RECURSE RCHEEVOS_SOURCES "${rcheevos_SOURCE_DIR}/src/*.c")
# The libretro helpers need libretro.h and aren't used here.
list(FILTER RCHEEVOS_SOURCES EXCLUDE REGEX "rc_libretro\\.c$")

add_library(rcheevos STATIC ${RCHEEVOS_SOURCES})
target_include_directories(rcheevos PUBLIC "${rcheevos_SOURCE_DIR}/include")
target_compile_definitions(rcheevos PUBLIC RC_CLIENT_SUPPORTS_HASH RC_DISABLE_LUA)
if(NOT MSVC)
    target_compile_options(rcheevos PRIVATE -w)
endif()

add_library(bk_retroachievements SHARED
    src/ra_mod.cpp
    src/http_android.cpp
    src/http_curl.cpp
    src/http_none.cpp)
# The mod loader looks for "<name>.so" next to the .nrm, without the usual "lib" prefix.
set_target_properties(bk_retroachievements PROPERTIES PREFIX "")
target_include_directories(bk_retroachievements PRIVATE src "${N64RECOMP_INCLUDE}")
target_compile_definitions(bk_retroachievements PRIVATE RA_MOD_VERSION="${RA_MOD_VERSION}")
target_link_libraries(bk_retroachievements PRIVATE rcheevos)

if(ANDROID)
    target_link_libraries(bk_retroachievements PRIVATE log dl)
    # 16 KB page alignment, required by newer Android devices.
    target_link_options(bk_retroachievements PRIVATE "-Wl,-z,max-page-size=16384")
else()
    find_package(CURL QUIET)
    if(CURL_FOUND)
        target_compile_definitions(bk_retroachievements PRIVATE RA_HTTP_CURL)
        target_link_libraries(bk_retroachievements PRIVATE CURL::libcurl)
    else()
        message(STATUS "libcurl not found: the desktop build of the mod will stay offline")
    endif()
    find_package(Threads REQUIRED)
    target_link_libraries(bk_retroachievements PRIVATE Threads::Threads)
endif()

if(NOT APPLE AND NOT MSVC)
    target_link_options(bk_retroachievements PRIVATE "-Wl,--exclude-libs,ALL" "-Wl,--no-undefined")
endif()
