#pragma once

#include <array>
#include <renderer/common.h>
#include <renderer/texture.h>

namespace renderer
{
	class Device;

	class Pipeline
	{
	public:
		enum class PrimitiveTopology : std::underlying_type_t<vk::PrimitiveTopology>
		{
			TRIANGLE_LIST = vk::PrimitiveTopology::eTriangleList,
			TRIANGLE_STRIP = vk::PrimitiveTopology::eTriangleStrip,
		};

		enum class CullMode : std::underlying_type_t<vk::CullModeFlagBits>
		{
			NONE = vk::CullModeFlagBits::eNone,
			FRONT = vk::CullModeFlagBits::eFront,
			BACK = vk::CullModeFlagBits::eBack
		};

		enum class FrontFace : std::underlying_type_t<vk::FrontFace>
		{
			COUNTER_CLOCKWISE = vk::FrontFace::eCounterClockwise,
			CLOCKWISE = vk::FrontFace::eClockwise
		};

		struct Desc
		{
			Texture::Format color_format;
			Texture::Format depth_format;
			PrimitiveTopology topology = PrimitiveTopology::TRIANGLE_LIST;
			CullMode cull_mode = CullMode::FRONT;
			FrontFace front_face = FrontFace::CLOCKWISE;
			uint32_t push_constants_size = 0;
		};

		Pipeline() = default;

		const Desc& get_desc() const { return _desc; };

	protected:
		Pipeline( vk::PipelineLayout layout, vk::Pipeline pipeline, const Pipeline::Desc& desc )
			: _layout( layout )
			, _pipeline( pipeline )
			, _desc( desc )
		{
		}

	private:
		vk::PipelineLayout _layout;
		vk::Pipeline _pipeline;
		Desc _desc;

		friend class CommandBuffer;
	};

	namespace raii
	{
		class Pipeline : public renderer::Pipeline
		{
		public:
			Pipeline() = default;

		private:
			Pipeline( vk::raii::PipelineLayout&& layout, vk::raii::Pipeline&& pipeline, const Pipeline::Desc& desc )
				: renderer::Pipeline( *layout, *pipeline, desc )
				, _layout( std::move( layout ) )
				, _pipeline( std::move( pipeline ) )

			{
			}

			vk::raii::PipelineLayout _layout = nullptr;
			vk::raii::Pipeline _pipeline = nullptr;
			friend class Device;
		};
	}

}
