cmake_minimum_required(VERSION 3.15)
project(roo2obj LANGUAGES C)

include(FetchContent)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

FetchContent_Declare(
	jansson
	GIT_REPOSITORY https://github.com/akheron/jansson.git
	GIT_TAG ed5cae4ed0621ef409510f94270c9f8f263736d0 # jansson 2.14.1 commit
)
set(JANSSON_EXAMPLES OFF CACHE BOOL "Compile example jansson applications" FORCE)
set(JANSSON_BUILD_DOCS OFF CACHE BOOL "Build jansson documentation" FORCE)
set(JANSSON_BUILD_MAN OFF CACHE BOOL "Build jansson man pages" FORCE)
set(JANSSON_WITHOUT_TESTS ON CACHE BOOL "Build jansson tests" FORCE)
set(JANSSON_INSTALL OFF CACHE BOOL "Generate jansson installation target" FORCE)
FetchContent_MakeAvailable(jansson)

add_executable(roo2obj roo2obj.c)

target_link_libraries(roo2obj PRIVATE jansson m)

target_include_directories(roo2obj PRIVATE
	"${jansson_BINARY_DIR}/include"
)
