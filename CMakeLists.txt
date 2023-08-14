cmake_minimum_required(VERSION 3.4 FATAL_ERROR)

set(NAME vulkan)

project(${NAME})

include_directories(${CMAKE_SOURCE_DIR}/includes)
include_directories(external/glm)
include_directories(external/imgui)


# Use FindVulkan module added with CMAKE 3.7
if (NOT CMAKE_VERSION VERSION_LESS 3.7.0)
	message(STATUS "Using module to find Vulkan")
	find_package(Vulkan)
endif()

IF(WIN32)
	IF (NOT Vulkan_FOUND)
		find_library(Vulkan_LIBRARY NAMES vulkan-1 vulkan PATHS ${CMAKE_SOURCE_DIR}/libs/vulkan)
		IF (Vulkan_LIBRARY)
			set(Vulkan_FOUND ON)
			MESSAGE("Using bundled Vulkan library version")
		ENDIF()
	ENDIF()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_WIN32_KHR")
ENDIF()


# Gather list of header and source files for compilation
file(GLOB CPP_FILES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
file(GLOB HPP_FILES ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

MESSAGE(${CPP_FILES})

add_subdirectory(base)

#c++
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build project, give it a name and includes list of file to be compiled
add_executable(${NAME} ${CPP_FILES} ${HPP_FILES})