cmake_minimum_required(VERSION 3.31)

project(vk-renderer CXX)

# Toolchain stuff: C++23 and fix some of CMake's nonsense for MSVC
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(/Zi /MP)
add_link_options(/DEBUG)

find_package(SDL3 CONFIG REQUIRED)
find_package(TBB CONFIG REQUIRED)
find_package(unofficial-shaderc CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

add_library(VkBoostrap STATIC)
target_sources(VkBoostrap PRIVATE third_party/vk-bootstrap/v1.4.305/VkBootstrap.cpp)
target_include_directories(VkBoostrap PUBLIC third_party/vk-bootstrap/v1.4.305)
target_link_libraries(VkBoostrap PUBLIC Vulkan::Vulkan)

add_library( VulkanHppModule )
target_sources( VulkanHppModule PRIVATE
  FILE_SET CXX_MODULES
  BASE_DIRS ${Vulkan_INCLUDE_DIR}
  FILES ${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm
)
target_compile_definitions( VulkanHppModule PUBLIC
  VULKAN_HPP_NO_SETTERS
  VULKAN_HPP_NO_CONSTRUCTORS
)
target_link_libraries( VulkanHppModule PUBLIC Vulkan::Vulkan )

add_library(renderer)
target_sources(renderer
	PRIVATE
		src/renderer/bindless.cpp
		src/renderer/buffer.cpp
		src/renderer/command_buffer.cpp
		src/renderer/device.cpp
		src/renderer/pipeline.cpp
		src/renderer/pipeline_manager.cpp
		src/renderer/sampler.cpp
		src/renderer/shader.cpp
		src/renderer/swapchain.cpp
		src/renderer/texture.cpp
		src/renderer/vma_impl.cpp
)
target_include_directories(renderer PUBLIC src)
target_link_libraries(renderer PRIVATE SDL3::SDL3 VkBoostrap unofficial::shaderc::shaderc TBB::tbb)
target_link_libraries(renderer PUBLIC Vulkan::Vulkan VulkanHppModule GPUOpen::VulkanMemoryAllocator)

if( TARGET Optick )
	target_compile_definitions(renderer PRIVATE USE_OPTICK)
	target_link_libraries(renderer PRIVATE Optick)
endif()

add_executable(clear_color)
target_sources(clear_color PRIVATE examples/clear_color.cpp)
target_link_libraries(clear_color PRIVATE renderer SDL3::SDL3)

if( TARGET Optick )
	target_compile_definitions(clear_color PRIVATE USE_OPTICK)
	target_link_libraries(clear_color PRIVATE Optick)
endif()

set_target_properties(clear_color PROPERTIES
    VS_DEBUGGER_ENVIRONMENT "VK_ADD_LAYER_PATH=${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
)
