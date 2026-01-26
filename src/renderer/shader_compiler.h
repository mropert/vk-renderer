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
		std::expected<raii::ShaderCode, std::string> compile( ShaderSource source ) const;
		// Compile from memory source
		std::expected<raii::ShaderCode, std::string> compile( ShaderSource source, std::string_view code ) const;

	private:
		struct Impl;
		std::unique_ptr<Impl> _impl;
	};
}
