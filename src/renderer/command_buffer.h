#pragma once

#include <array>
#include <optional>
#include <renderer/common.h>
#include <renderer/pipeline.h>
#include <renderer/texture.h>

namespace renderer
{
	class BindlessManagerBase;
	class Buffer;
	class TextureView;

	// Barriers derive memory accesses from the stages: producers make all the writes of their stages available, consumers
	// get them visible to all the reads and writes of theirs
	enum class Stage : uint32_t
	{
		NONE = 0,
		INDIRECT = 1 << 0,
		INDEX = 1 << 1,
		VERTEX = 1 << 2, // All pre-rasterization shaders: vertex, task and mesh
		FRAGMENT = 1 << 3,
		DEPTH = 1 << 4, // Early and late fragment tests
		COLOR = 1 << 5, // Color attachment output, which is also where MSAA resolves happen, depth included
		COMPUTE = 1 << 6,
		TRANSFER = 1 << 7,
		HOST = 1 << 8,
		ANY_SHADER = VERTEX | FRAGMENT | COMPUTE,
		ALL = INDIRECT | INDEX | ANY_SHADER | DEPTH | COLOR | TRANSFER | HOST
	};

	constexpr Stage operator|( Stage lhs, Stage rhs )
	{
		return Stage( std::to_underlying( lhs ) | std::to_underlying( rhs ) );
	}

	constexpr Stage operator&( Stage lhs, Stage rhs )
	{
		return Stage( std::to_underlying( lhs ) & std::to_underlying( rhs ) );
	}

	struct RenderAttachment
	{
		// Depth only supports SAMPLE_ZERO everywhere, the other modes depend on the device
		enum class ResolveMode : std::underlying_type_t<vk::ResolveModeFlagBits>
		{
			SAMPLE_ZERO = std::to_underlying( vk::ResolveModeFlagBits::eSampleZero ),
			AVERAGE = std::to_underlying( vk::ResolveModeFlagBits::eAverage ),
			MIN = std::to_underlying( vk::ResolveModeFlagBits::eMin ),
			MAX = std::to_underlying( vk::ResolveModeFlagBits::eMax )
		};

		TextureView target;
		TextureView resolve_target;
		std::optional<std::array<float, 4>> clear_value;
		ResolveMode resolve_mode = ResolveMode::AVERAGE;
	};

	struct RenderInfo
	{
		RenderAttachment color_target;
		RenderAttachment depth_target;
		Extent2D extent; // Defaults to color_target or depth_target extent if left blank
	};

	class CommandBuffer
	{
	public:
		// Closes the debug label range opened by CommandBuffer::debug_label() when going out of scope
		class [[nodiscard]] DebugLabelScope
		{
		public:
			DebugLabelScope( const DebugLabelScope& ) = delete;
			DebugLabelScope& operator=( const DebugLabelScope& ) = delete;
			~DebugLabelScope();

		private:
			explicit DebugLabelScope( CommandBuffer* cmd )
				: _cmd( cmd )
			{
			}

			CommandBuffer* _cmd; // null when debug labels aren't supported

			friend class CommandBuffer;
		};

		void begin();
		void end();
		void reset();

		// Images live in GENERAL, one side of the transition must be GENERAL. Coming from UNDEFINED or PRESENT_SRC
		// discards the content. Leaving PRESENT_SRC only syncs rendering and transfers with the swapchain acquire, other
		// uses need a barrier from COLOR after it (eg: compute writes).
		void transition_texture( const Texture& tex, Texture::Layout from, Texture::Layout to );
		// Global memory barrier, covers all buffers and textures
		void barrier( Stage producers, Stage consumers );

		void blit_texture( const Texture& src, const Texture& dst, int src_mip = 0, int dst_mip = 0, bool linear_filter = false );
		// Blits the top left src_extent of src_mip to the top left dst_extent of dst_mip
		void blit_texture( const Texture& src,
						   const Texture& dst,
						   int src_mip,
						   Extent2D src_extent,
						   int dst_mip,
						   Extent2D dst_extent,
						   bool linear_filter = false );

		void copy_buffer( const Buffer& src, std::size_t offset, std::size_t size, const Buffer& dest, std::size_t dest_offset = 0 );
		// offset must be a multiple of the texel block size (8 or 16 bytes for BC formats)
		void copy_buffer_to_texture( const Buffer& buffer, std::size_t offset, const Texture& tex, int mip = 0 );
		// Copies to the top left extent of mip, tightly packed in the buffer. For block compressed formats, extent must be
		// a multiple of the block size or reach the mip's edge
		void copy_buffer_to_texture( const Buffer& buffer, std::size_t offset, const Texture& tex, int mip, Extent2D extent );
		// Copies the whole mip, tightly packed in the buffer
		void copy_texture_to_buffer( const Texture& tex, int mip, const Buffer& buffer, std::size_t offset = 0 );
		void fill_buffer( const Buffer& buffer, size_t offset, size_t size, uint32_t value );

		void begin_rendering( RenderInfo info );
		void end_rendering();

		void bind_pipeline( const Pipeline& pipeline, const BindlessManagerBase& bindless_manager );

		// Sets all dynamic state of graphics pipelines, must be called before drawing
		void set_dynamic_state( const Pipeline::DynamicState& state );

		void set_scissor( Extent2D extent );
		// Will flip Y axis on the NDC behind the scene to be consistent with every other rendering API in town
		void set_viewport( Extent2D extent );
		void set_cull_mode( Pipeline::CullMode mode );
		void set_depth_test( bool test );
		void set_depth_write( bool write );
		// Zero factors disable depth bias
		void set_depth_bias( float constant_factor, float slope_factor, float clamp = 0.f );

		template <typename T>
		void push_constants( const Pipeline& pipeline, const T& data )
		{
			push_constants( pipeline, &data, sizeof( T ) );
		}

		void bind_index_buffer( const Buffer& index_buffer );

		void draw( uint32_t count );
		void draw_indexed( uint32_t count, uint32_t instance_count = 1, uint32_t first_index = 0, uint32_t first_instance = 0 );
		void draw_indexed_indirect( const Buffer& buffer, size_t offset, uint32_t count, uint32_t stride );
		void draw_indexed_indirect( const Buffer& buffer,
									size_t offset,
									const Buffer& count_buffer,
									size_t count_offset,
									uint32_t max_draws,
									uint32_t stride );
		void draw_mesh_tasks( uint32_t x, uint32_t y, uint32_t z );
		void draw_mesh_tasks_indirect( const Buffer& buffer,
									   size_t offset,
									   const Buffer& count_buffer,
									   size_t count_offset,
									   uint32_t max_draws,
									   uint32_t stride );

		void dispatch( uint32_t x, uint32_t y, uint32_t z );

		// Statistic queries
		// No-op if query objects are null/empty to simplify handling devices without support for statistics (like MoltenVK)
		void reset_query( TimestampQuery query, uint32_t first, uint32_t count );
		void write_timestamp( TimestampQuery query, uint32_t index );
		void reset_query( StatisticsQuery query );
		void begin_query( StatisticsQuery query );
		void end_query( StatisticsQuery query );

		// Named (and optionally colored) range showing up in debuggers/profilers (RenderDoc, Nsight...)
		// Color is RGBA, all zeroes means no color. No-op if VK_EXT_debug_utils isn't supported.
		DebugLabelScope debug_label( const char* name, const std::array<float, 4>& color = { } );

		// Get the underlying renderer buffer, for integration with 3rd party (eg: imgui)
		VkCommandBuffer get_impl() const { return *_cmd_buffer; }

	private:
		CommandBuffer( vk::raii::CommandBuffer cmd_buffer, bool debug_labels_support )
			: _cmd_buffer( std::move( cmd_buffer ) )
			, _debug_labels_support( debug_labels_support )
		{
		}

		void push_constants( const Pipeline& pipeline, const void* data, std::size_t size );

		vk::raii::CommandBuffer _cmd_buffer;
		void* _optick_previous = nullptr;
		bool _debug_labels_support = false;

		friend class Device;
		friend class Swapchain;
		friend class Texture;
	};
}
