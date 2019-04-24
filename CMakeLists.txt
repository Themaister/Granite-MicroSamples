cmake_minimum_required(VERSION 3.5)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_C_STANDARD 99)
project(Granite-Samples LANGUAGES CXX C)

# We are overriding settings in subdirectories.
# Avoid warnings.
if (${CMAKE_VERSION} VERSION_GREATER "3.13.0")
   message("Setting CMake policy CMP0077.")
   cmake_policy(SET CMP0077 NEW)
endif()

set(GRANITE_VULKAN_ONLY ON CACHE BOOL "" FORCE)
set(GRANITE_VULKAN_MT ON CACHE BOOL "" FORCE)
set(GRANITE_TOOLS OFF CACHE BOOL "" FORCE)

add_subdirectory(Granite EXCLUDE_FROM_ALL)

find_package(SDL2)
if (SDL2_FOUND)
    message("Found SDL2, adding samples which use WSI.")
else()
    message("Did not find SDL2, skipping samples which use WSI.")
endif()

add_granite_offline_tool(01-device-creation 01_device_creation.cpp)
add_granite_offline_tool(02-object-creation 02_object_creation.cpp)
add_granite_offline_tool(03-frame-contexts 03_frame_contexts.cpp)
add_granite_offline_tool(04-shaders-and-programs 04_shaders_and_programs.cpp)
add_granite_offline_tool(05-descriptor-sets-and-binding-model 05_descriptor_sets_and_binding_model.cpp)

if (SDL2_FOUND)
    add_granite_offline_tool(06-wsi-sdl2 06_wsi_sdl2.cpp)
    target_link_libraries(06-wsi-sdl2 PRIVATE SDL2::SDL2)

    add_granite_offline_tool(07-linear-allocators 07_linear_allocators.cpp)
    target_link_libraries(07-linear-allocators PRIVATE SDL2::SDL2)

    add_granite_offline_tool(08-render-passes 08_render_passes.cpp)
    target_link_libraries(08-render-passes PRIVATE SDL2::SDL2)
endif()

