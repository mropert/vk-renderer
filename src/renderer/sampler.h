#pragma once


#pragma once

#include <renderer/common.h>

namespace renderer
{
	class Device;

	namespace raii
	{
		class Sampler;
	}

	class Sampler
	{
	public:
		enum class Filter : std::underlying_type_t<vk::Filter>
		{
			NEAREST = std::to_underlying( vk::Filter::eNearest ),
			LINEAR = std::to_underlying( vk::Filter::eLinear ),
			CUBIC = std::to_underlying( vk::Filter::eCubicEXT )
		};

		enum class ReductionMode : std::underlying_type_t<vk::SamplerReductionMode>
		{
			AVERAGE = std::to_underlying( vk::SamplerReductionMode::eWeightedAverage ),
			MIN = std::to_underlying( vk::SamplerReductionMode::eMin ),
			MAX = std::to_underlying( vk::SamplerReductionMode::eMax ),
		};

		enum class AddressMode : std::underlying_type_t<vk::SamplerAddressMode>
		{
			REPEAT = std::to_underlying( vk::SamplerAddressMode::eRepeat ),
			CLAMP = std::to_underlying( vk::SamplerAddressMode::eClampToEdge ),
			BORDER = std::to_underlying( vk::SamplerAddressMode::eClampToBorder )
		};

		struct Desc
		{
			Filter filter = Filter::NEAREST;
			AddressMode address_mode = AddressMode::REPEAT;
			ReductionMode reduction_mode = ReductionMode::AVERAGE;
			CompareOp compare_op = CompareOp::NEVER;
			bool white_border = false; // for BORDER mode, use black or white opaque color
		};

		Sampler() = default;

	private:
		explicit Sampler( vk::Sampler sampler )
			: _sampler( sampler )
		{
		}

		vk::Sampler _sampler;

		friend class BindlessManagerBase;
		friend class CommandBuffer;
		friend class Device;
		friend class raii::Sampler;
	};

	namespace raii
	{
		class Sampler
		{
		public:
			Sampler() = default;
			operator renderer::Sampler() const { return renderer::Sampler( _sampler ); }

		private:
			explicit Sampler( vk::raii::Sampler sampler )
				: _sampler( std::move( sampler ) )
			{
			}

			friend class renderer::Device;
			vk::raii::Sampler _sampler = nullptr;
		};
	}
}
