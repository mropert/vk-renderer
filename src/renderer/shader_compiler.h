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

		std::expected<ShaderSourceHash, std::string> hash_source( const ShaderSource& source ) const;

	private:
		struct Impl;
		std::unique_ptr<Impl> _impl;
	};

	class ShaderCache
	{
	public:
		explicit ShaderCache( std::filesystem::path base_dir );

		// source will be moved from on success, left untouched on failure
		std::optional<raii::ShaderCode> get( ShaderSource& source, ShaderSourceHash key ) const;
		void add( ShaderSourceHash key, const raii::ShaderCode& code ) const;

	private:
		std::filesystem::path get_path( ShaderSourceHash key ) const;

		std::filesystem::path _base_dir;
	};
}
