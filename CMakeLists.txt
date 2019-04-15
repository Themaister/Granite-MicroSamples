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

add_granite_offline_tool(01-device-creation 01_device_creation.cpp)
