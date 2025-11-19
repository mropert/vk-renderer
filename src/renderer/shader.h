#pragma once

#include <expected>
#include <renderer/common.h>
#include <string_view>

namespace renderer
{
	enum class ShaderStage : std::underlying_type_t<vk::ShaderStageFlagBits>
	{
		VERTEX = vk::ShaderStageFlagBits::eVertex,
		FRAGMENT = vk::ShaderStageFlagBits::eFragment
	};

	inline constexpr ShaderStage operator|( ShaderStage lhs, ShaderStage rhs )
	{
		return ShaderStage( std::to_underlying( lhs ) | std::to_underlying( rhs ) );
	}

	namespace raii
	{
		class ShaderCode;
	}

	class ShaderCompiler
	{
	public:
		ShaderCompiler();
		~ShaderCompiler();

		std::expected<raii::ShaderCode, std::string> compile( ShaderStage stage, std::string_view source, std::string filename ) const;

	private:
		struct Impl;
		std::unique_ptr<Impl> _impl;
	};

	namespace raii
	{
		class ShaderCode
		{
		public:
			ShaderCode() = default;

			const uint32_t* get_data() const { return _bytes.data(); }
			uint32_t get_size() const { return _bytes.size(); }
			ShaderStage get_stage() const { return _stage; }
			const std::string& get_filename() const { return _filename; }

		private:
			ShaderCode( ShaderStage stage, std::vector<uint32_t> bytes, std::string filename )
				: _stage( stage )
				, _bytes( std::move( bytes ) )
				, _filename( std::move( filename ) )
			{
			}

			friend class ShaderCompiler;

			std::vector<uint32_t> _bytes;
			std::string _filename;
			ShaderStage _stage;
		};
	}
}
