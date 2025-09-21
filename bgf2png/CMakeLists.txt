cmake_minimum_required(VERSION 3.15)
project(bgf2png LANGUAGES C)

include(FetchContent)

set(SKIP_INSTALL_ALL TRUE CACHE BOOL "" FORCE)

FetchContent_Declare(
	zlib
	GIT_REPOSITORY https://github.com/madler/zlib.git
	GIT_TAG 51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf # zlib 1.3.1 commit
)
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "Enable Zlib Examples" FORCE)
FetchContent_MakeAvailable(zlib)

# Minimal ZLIBConfig.cmake to satisfy libpng's call to find_package
set(_ZLIB_PKG "${zlib_BINARY_DIR}")
file(MAKE_DIRECTORY "${_ZLIB_PKG}")
file(WRITE "${_ZLIB_PKG}/ZLIBConfig.cmake"
"## Auto-generated build-tree package: satisfy find_package(ZLIB)
if(TARGET zlibstatic AND NOT TARGET ZLIB::ZLIB)
  add_library(ZLIB::ZLIB ALIAS zlibstatic)
elseif(TARGET zlib AND NOT TARGET ZLIB::ZLIB)
  add_library(ZLIB::ZLIB ALIAS zlib)
endif()
set(ZLIB_FOUND TRUE CACHE BOOL \"\" FORCE)
set(ZLIB_INCLUDE_DIRS \"${zlib_BINARY_DIR};${zlib_SOURCE_DIR}\" CACHE STRING \"\" FORCE)
set(ZLIB_INCLUDE_DIR \"${zlib_BINARY_DIR};${zlib_SOURCE_DIR}\" CACHE STRING \"\" FORCE)
set(ZLIB_LIBRARIES ZLIB::ZLIB CACHE STRING \"\" FORCE)
set(ZLIB_LIBRARY ZLIB::ZLIB CACHE STRING \"\" FORCE)
")

# Make libpng's find_package(ZLIB) pick this up (config mode)
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)
set(ZLIB_DIR "${_ZLIB_PKG}" CACHE PATH "" FORCE)

FetchContent_Declare(
	libpng
	GIT_REPOSITORY https://github.com/pnggroup/libpng.git
	GIT_TAG 2b978915d82377df13fcbb1fb56660195ded868a # libpng 1.6.50 commit
)
set(PNG_SHARED OFF CACHE BOOL "Build libpng as a shared library" FORCE)
set(PNG_TESTS OFF CACHE BOOL "Build PNG Tests" FORCE)
set(PNG_FRAMEWORK OFF CACHE BOOL "Build libpng as a framework bundle" FORCE)
FetchContent_MakeAvailable(libpng)

add_executable(bgf2png bgf2png.c)

target_link_libraries(bgf2png PRIVATE png_static zlibstatic)

target_include_directories(bgf2png PRIVATE
	${CMAKE_SOURCE_DIR}
	"${zlib_SOURCE_DIR}" "${zlib_BINARY_DIR}"
	"${libpng_SOURCE_DIR}" "${libpng_BINARY_DIR}"
)
