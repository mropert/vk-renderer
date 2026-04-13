#pragma once

// Global Module Fragment (GMF)

#include <SDL3/SDL.h>
#include <vma/vk_mem_alloc.h>

#if defined( __INTELLISENSE__ ) || !defined( VULKAN_HPP_ENABLE_STD_MODULE )
#include <algorithm>
#include <array>
#include <compare>
#include <condition_variable>
#include <expected>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>
#endif

#if defined( __INTELLISENSE__ )
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif
