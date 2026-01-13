#pragma once


#pragma once

#include <renderer/common.h>

namespace renderer
{
	namespace raii
	{
		class Sampler;
	}

	class Sampler
	{
	public:
		enum class Filter : std::underlying_type_t<vk::Filter>
		{
			NEAREST = vk::Filter::eNearest,
			LINEAR = vk::Filter::eLinear,
			CUBIC = vk::Filter::eCubicEXT
		};

		enum class ReductionMode : std::underlying_type_t<vk::SamplerReductionMode>
		{
			AVERAGE = vk::SamplerReductionMode::eWeightedAverage,
			MIN = vk::SamplerReductionMode::eMin,
			MAX = vk::SamplerReductionMode::eMax,
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

			friend class Device;
			vk::raii::Sampler _sampler = nullptr;
		};
	}
}
