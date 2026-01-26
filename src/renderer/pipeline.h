#pragma once

#include <array>
#include <renderer/common.h>
#include <renderer/texture.h>
#include <variant>

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

		enum class Type
		{
			Compute,
			Graphics
		};

		struct Desc
		{
			// Graphics pipelines only
			Texture::Format color_format;
			Texture::Format depth_format;
			PrimitiveTopology topology = PrimitiveTopology::TRIANGLE_LIST;
			CullMode cull_mode = CullMode::BACK;
			FrontFace front_face = FrontFace::COUNTER_CLOCKWISE;
			// Compute & graphics pipelines
			uint32_t push_constants_size = 0;
		};

		Pipeline() = default;

		const Desc& get_desc() const { return _desc; };
		Type get_type() const { return _type; }

	protected:
		Pipeline( vk::PipelineLayout layout,
				  vk::Pipeline pipeline,
				  const Pipeline::Desc& desc,
				  vk::ShaderStageFlags used_stages,
				  Type type )
			: _layout( layout )
			, _pipeline( pipeline )
			, _desc( desc )
			, _used_stages( used_stages )
			, _type( type )
		{
		}

	private:
		vk::PipelineLayout _layout;
		vk::Pipeline _pipeline;
		Desc _desc;
		vk::ShaderStageFlags _used_stages;
		Type _type;

		friend class CommandBuffer;
	};

	namespace raii
	{
		class Pipeline : public renderer::Pipeline
		{
		public:
			Pipeline() = default;

		private:
			Pipeline( vk::raii::PipelineLayout&& layout,
					  vk::raii::Pipeline&& pipeline,
					  const Pipeline::Desc& desc,
					  vk::ShaderStageFlags used_stages,
					  Type type )
				: renderer::Pipeline( *layout, *pipeline, desc, used_stages, type )
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
