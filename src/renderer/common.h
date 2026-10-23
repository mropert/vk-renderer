#pragma once

// XXX: vulkan_hpp module imports the std module which generates weird behaviour when other libraries #include the STL
// As a workaround we make sur they are included first
#include <SDL3/SDL.h>
#include <array>
#include <atomic>
#include <expected>
#include <initializer_list>
#include <memory>
#include <optional>
#include <queue>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <version>
// vcpkg installs under a different path in Windows vs other OSs...
#ifdef _WIN32
#include <vma/vk_mem_alloc.h>
#else
#include <vk_mem_alloc.h>
#endif

#if defined( __INTELLISENSE__ ) || !defined( __cpp_modules ) || ( __cpp_modules < 201907L )
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#else
// Also include every other bit of the STL that we use or else it will fail for client who haven't migrated to `import std`.
// This is horrible but the whole module design is based on virality...
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory_resource>
#include <mutex>
#include <numeric>
#include <random>
#include <shared_mutex>
#include <source_location>
#include <unordered_map>
#include <variant>

import vulkan;
#endif

namespace sdl::raii
{
	struct ResourceDeleter
	{
		void operator()( ::SDL_Surface* surface ) const { SDL_DestroySurface( surface ); }
		void operator()( ::SDL_Window* window ) const { SDL_DestroyWindow( window ); }
	};
	using Surface = std::unique_ptr<::SDL_Surface, ResourceDeleter>;
	using Window = std::unique_ptr<::SDL_Window, ResourceDeleter>;
}

namespace renderer
{
	enum class MemoryResource : uint8_t
	{
		TEXTURE,
		STORAGE_BUFFER,
		INDEX_BUFFER,
		OTHER_BUFFER, // Staging, readback...
		COUNT
	};

	// Memory allocated through a Device, in bytes. BAR (device local and host visible memory) is also counted in VRAM.
	struct MemoryUsage
	{
		std::array<std::size_t, std::to_underlying( MemoryResource::COUNT )> resources { };
		std::size_t vram = 0;
		std::size_t bar = 0;
		std::size_t ram = 0;
	};

	// Thread safe, resources can be created and destroyed from any thread
	class MemoryTracker
	{
	public:
		void add( MemoryResource resource, VkMemoryPropertyFlags memory_flags, std::size_t size )
		{
			_resources[ std::to_underlying( resource ) ] += size;
			const bool device_local = ( memory_flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ) != 0;
			const bool host_visible = ( memory_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ) != 0;
			( device_local ? _vram : _ram ) += size;
			if ( device_local && host_visible )
			{
				_bar += size;
			}
		}

		void remove( MemoryResource resource, VkMemoryPropertyFlags memory_flags, std::size_t size )
		{
			_resources[ std::to_underlying( resource ) ] -= size;
			const bool device_local = ( memory_flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ) != 0;
			const bool host_visible = ( memory_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ) != 0;
			( device_local ? _vram : _ram ) -= size;
			if ( device_local && host_visible )
			{
				_bar -= size;
			}
		}

		MemoryUsage get_usage() const
		{
			MemoryUsage usage { .vram = _vram, .bar = _bar, .ram = _ram };
			for ( size_t i = 0; i < _resources.size(); ++i )
			{
				usage.resources[ i ] = _resources[ i ];
			}
			return usage;
		}

	private:
		std::array<std::atomic<std::size_t>, std::to_underlying( MemoryResource::COUNT )> _resources { };
		std::atomic<std::size_t> _vram = 0;
		std::atomic<std::size_t> _bar = 0;
		std::atomic<std::size_t> _ram = 0;
	};
}

namespace vma::raii
{
	struct AllocatorDeleter
	{
		void operator()( ::VmaAllocator allocator ) const { vmaDestroyAllocator( allocator ); }
	};
	using Allocator = std::unique_ptr<::VmaAllocator_T, AllocatorDeleter>;

	struct Allocation
	{
		::VmaAllocator allocator;
		::VmaAllocation allocation;
		::VmaAllocationInfo info;
		renderer::MemoryTracker* tracker = nullptr;
		renderer::MemoryResource resource = renderer::MemoryResource::OTHER_BUFFER;

		void destroy( ::VkBuffer buffer ) const
		{
			if ( buffer )
			{
				vmaDestroyBuffer( allocator, buffer, allocation );
				untrack();
			}
		}
		void destroy( ::VkImage image ) const
		{
			if ( image )
			{
				vmaDestroyImage( allocator, image, allocation );
				untrack();
			}
		}

	private:
		void untrack() const
		{
			if ( tracker )
			{
				::VkMemoryPropertyFlags memory_flags = 0;
				vmaGetMemoryTypeProperties( allocator, info.memoryType, &memory_flags );
				tracker->remove( resource, memory_flags, info.size );
			}
		}
	};

	struct ResourceDeleter
	{
		::VmaAllocator allocator;
		::VmaAllocation allocation;
		::VmaAllocationInfo info;
		void operator()( ::VkBuffer buffer ) const { vmaDestroyBuffer( allocator, buffer, allocation ); }
		void operator()( ::VkImage image ) const { vmaDestroyImage( allocator, image, allocation ); }
	};
	template <typename T>
	using Resource = std::unique_ptr<std::remove_pointer_t<T>, ResourceDeleter>;

	template <typename T>
	auto make( ::VmaAllocator allocator, T object, ::VmaAllocation allocation, const ::VmaAllocationInfo& info )
	{
		return Resource<T> { object, ResourceDeleter { allocator, allocation, info } };
	}

	using Buffer = Resource<::VkBuffer>;
	using Image = Resource<::VkImage>;
}

namespace renderer
{
	inline constexpr int MAX_FRAMES_IN_FLIGHT = 2;
	using Extent2D = ::vk::Extent2D;

	// Vulkan types that are worth the code to wrap them, it's just handles for us
	using Fence = ::vk::Fence;
	using StatisticsQuery = ::vk::QueryPool;
	using TimestampQuery = ::vk::QueryPool;

	enum class CompareOp : std::underlying_type_t<vk::CompareOp>
	{
		NEVER = std::to_underlying( vk::CompareOp::eNever ),
		LESS = std::to_underlying( vk::CompareOp::eLess ),
		LESS_OR_EQUAL = std::to_underlying( vk::CompareOp::eLessOrEqual ),
		GREATER = std::to_underlying( vk::CompareOp::eGreater ),
		GREATER_OR_EQUAL = std::to_underlying( vk::CompareOp::eGreaterOrEqual )
	};

	class Error : public std::runtime_error
	{
	public:
		Error( std::error_code code, VkResult result )
			: std::runtime_error( code.message() )
			, _error( result )
		{
		}

		explicit Error( const std::string& msg )
			: std::runtime_error( msg )
		{
		}

		Error( const std::string& msg, VkResult error )
			: std::runtime_error( msg )
			, _error( error )
		{
		}

		Error( const std::string& msg, vk::Result error )
			: std::runtime_error( msg )
			, _error( static_cast<VkResult>( error ) )
		{
		}

		VkResult get_vulkan_error() const { return _error; }

	private:
		VkResult _error = VK_SUCCESS;
	};

	namespace raii
	{
		using Fence = ::vk::raii::Fence;
		using StatisticsQuery = ::vk::raii::QueryPool;
		using TimestampQuery = ::vk::raii::QueryPool;
	}
}
