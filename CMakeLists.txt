cmake_minimum_required(VERSION 3.16)
project(integratedbodyphysics C CXX)
set(CMAKE_CXX_STANDARD 17)

# Jolt Physics (MIT licence), built with SSE4.2 only so it runs on any CPU from the last decade.
include(FetchContent)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(ENABLE_OBJECT_STREAM OFF CACHE BOOL "" FORCE)
set(GENERATE_DEBUG_SYMBOLS OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
foreach(opt USE_AVX USE_AVX2 USE_AVX512 USE_LZCNT USE_TZCNT USE_F16C USE_FMADD)
    set(${opt} OFF CACHE BOOL "" FORCE)
endforeach()
FetchContent_Declare(JoltPhysics GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics GIT_TAG v5.3.0 SOURCE_SUBDIR Build)
FetchContent_MakeAvailable(JoltPhysics)
# MinHook (BSD-2): the GPU probe's code hooks (gpuprobe.cpp).
FetchContent_Declare(minhook GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git GIT_TAG master GIT_SHALLOW TRUE
    SOURCE_SUBDIR headers-only)
FetchContent_MakeAvailable(minhook)

add_library(integratedbodyphysics SHARED src/jiggle.cpp src/physics.cpp src/gpuprobe.cpp src/wavepatch.cpp
    ${minhook_SOURCE_DIR}/src/buffer.c ${minhook_SOURCE_DIR}/src/hook.c ${minhook_SOURCE_DIR}/src/trampoline.c
    ${minhook_SOURCE_DIR}/src/hde/hde64.c)
target_include_directories(integratedbodyphysics PRIVATE ${minhook_SOURCE_DIR}/include)
set_target_properties(integratedbodyphysics PROPERTIES PREFIX "" OUTPUT_NAME integratedbodyphysics)
target_compile_definitions(integratedbodyphysics PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(integratedbodyphysics PRIVATE Jolt)
if(MINGW)
    target_compile_options(integratedbodyphysics PRIVATE -O2 -Wall)
    target_link_options(integratedbodyphysics PRIVATE -static -s)
endif()
