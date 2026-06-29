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

		// private:
		vk::Sampler _sampler;

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
			operator renderer::Sampler() const { return { _sampler }; }

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
