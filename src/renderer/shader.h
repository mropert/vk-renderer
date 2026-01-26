#pragma once

#include <renderer/common.h>

namespace renderer
{
	enum class ShaderStage : std::underlying_type_t<vk::ShaderStageFlagBits>
	{
		VERTEX = vk::ShaderStageFlagBits::eVertex,
		FRAGMENT = vk::ShaderStageFlagBits::eFragment,
		COMPUTE = vk::ShaderStageFlagBits::eCompute,
		TASK = vk::ShaderStageFlagBits::eTaskEXT,
		MESH = vk::ShaderStageFlagBits::eMeshEXT
	};

	inline constexpr ShaderStage operator|( ShaderStage lhs, ShaderStage rhs )
	{
		return ShaderStage( std::to_underlying( lhs ) | std::to_underlying( rhs ) );
	}

	struct ShaderSource
	{
		struct Define
		{
			std::string key;
			std::string value;
		};

		std::string path;
		ShaderStage stage;
		std::vector<Define> defines;
	};

	class ShaderCompiler;

	namespace raii
	{
		class ShaderCode
		{
		public:
			ShaderCode() = default;

			const uint32_t* get_data() const { return _bytes.data(); }
			uint32_t get_size() const { return _bytes.size(); }
			uint32_t get_size_bytes() const { return _bytes.size() * sizeof( uint32_t ); }
			const ShaderSource& get_source() const { return _source; }

		private:
			ShaderCode( ShaderSource source, std::vector<uint32_t> bytes )
				: _source( std::move( source ) )
				, _bytes( std::move( bytes ) )
			{
			}

			friend ShaderCompiler;

			ShaderSource _source;
			std::vector<uint32_t> _bytes;
		};
	}
}
