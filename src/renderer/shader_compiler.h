#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <renderer/common.h>
#include <renderer/shader.h>
#include <string_view>

namespace renderer
{
	class ShaderCompiler
	{
	public:
		explicit ShaderCompiler( std::filesystem::path base_dir );
		~ShaderCompiler();

		const std::filesystem::path& get_base_directory() const;

		// Read file and compile
		std::expected<raii::ShaderCode, std::string> compile( ShaderStage stage, std::string_view filename ) const;
		// Compile from memory source
		std::expected<raii::ShaderCode, std::string> compile( ShaderStage stage, std::string_view source, std::string filename ) const;

	private:
		struct Impl;
		std::unique_ptr<Impl> _impl;
	};
}
