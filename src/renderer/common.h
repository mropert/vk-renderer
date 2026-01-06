#pragma once

// XXX: vulkan_hpp module imports the std module which generates weird behaviour when other libraries #include the STL
// As a workaround we make sur they are included first
#include <SDL3/SDL.h>
#include <array>
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
#include <vma/vk_mem_alloc.h>

#if defined( __INTELLISENSE__ )
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
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

		void destroy( ::VkBuffer buffer ) const
		{
			if ( buffer )
			{
				vmaDestroyBuffer( allocator, buffer, allocation );
			}
		}
		void destroy( ::VkImage image ) const
		{
			if ( image )
			{
				vmaDestroyImage( allocator, image, allocation );
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
	using QueryPool = ::vk::QueryPool;

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

	private:
		VkResult _error = VK_SUCCESS;
	};

	namespace raii
	{
		using Fence = ::vk::raii::Fence;
		using QueryPool = ::vk::raii::QueryPool;
	}
}
