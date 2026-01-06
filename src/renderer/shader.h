#pragma once

#include <renderer/common.h>
#include <span>

namespace renderer
{
	enum class ShaderStage : std::underlying_type_t<vk::ShaderStageFlagBits>
	{
		VERTEX = vk::ShaderStageFlagBits::eVertex,
		FRAGMENT = vk::ShaderStageFlagBits::eFragment,
		MESH = vk::ShaderStageFlagBits::eMeshEXT
	};

	inline constexpr ShaderStage operator|( ShaderStage lhs, ShaderStage rhs )
	{
		return ShaderStage( std::to_underlying( lhs ) | std::to_underlying( rhs ) );
	}

	struct ShaderSource
	{
		std::string path;
		ShaderStage stage;
	};

	class ShaderCompiler;

	namespace raii
	{
		class ShaderCode;
	}

	class ShaderCode
	{
	public:
		constexpr ShaderCode() = default;

	private:
		constexpr ShaderCode( std::span<const uint32_t> blob, ShaderStage stage )
			: _blob( blob )
			, _stage( stage )
		{
		}

		friend raii::ShaderCode;
		friend class Device;
		std::span<const uint32_t> _blob;
		ShaderStage _stage;
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

			operator renderer::ShaderCode() const { return { _bytes, _stage }; }

		private:
			ShaderCode( ShaderStage stage, std::vector<uint32_t> bytes, std::string filename )
				: _stage( stage )
				, _bytes( std::move( bytes ) )
				, _filename( std::move( filename ) )
			{
			}

			friend ShaderCompiler;

			std::vector<uint32_t> _bytes;
			std::string _filename;
			ShaderStage _stage;
		};
	}
}
