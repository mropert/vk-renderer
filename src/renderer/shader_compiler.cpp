#include "shader_compiler.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <glslang/build_info.h>
#include <renderer/details/profiler.h>
#include <shaderc/shaderc.hpp>
#include <spirv-tools/libspirv.h>

namespace
{
	struct ShaderSource
	{
		std::string filename;
		std::string content;
	};

	ShaderSource read_source( const std::filesystem::path& path )
	{
		std::ifstream istream( path );
		if ( !istream )
		{
			return { };
		}
		std::string source { std::istreambuf_iterator<char>( istream ), std::istreambuf_iterator<char>() };
		return { path.string(), std::move( source ) };
	}

	struct ShaderIncluder final : public shaderc::CompileOptions::IncluderInterface
	{
		explicit ShaderIncluder( const std::filesystem::path& dir )
			: base_dir( dir )
		{
		}

		shaderc_include_result* GetInclude( const char* requested_source, shaderc_include_type, const char*, size_t ) override
		{
			auto source = new ShaderSource( read_source( base_dir / requested_source ) );
			if ( source->filename.empty() )
			{
				source->content = std::format( "Couldn't open shader include file '{}'", requested_source );
			}
			auto result = new shaderc_include_result;
			result->source_name = source->filename.empty() ? nullptr : source->filename.c_str();
			result->source_name_length = source->filename.size();
			result->content = source->content.c_str();
			result->content_length = source->content.size();
			result->user_data = source;
			return result;
		}

		void ReleaseInclude( shaderc_include_result* data ) override
		{
			delete static_cast<ShaderSource*>( data->user_data );
			delete data;
		}

		const std::filesystem::path& base_dir;
	};

	shaderc_shader_kind get_shader_kind( renderer::ShaderStage stage )
	{
		switch ( stage )
		{
			case renderer::ShaderStage::COMPUTE:
				return shaderc_shader_kind::shaderc_compute_shader;
			case renderer::ShaderStage::FRAGMENT:
				return shaderc_shader_kind::shaderc_fragment_shader;
			case renderer::ShaderStage::MESH:
				return shaderc_shader_kind::shaderc_mesh_shader;
			case renderer::ShaderStage::TASK:
				return shaderc_shader_kind::shaderc_task_shader;
			case renderer::ShaderStage::VERTEX:
				return shaderc_shader_kind::shaderc_vertex_shader;
			default:
				throw renderer::Error( "invalid shader type" );
		}
	}

	constexpr auto SHADER_TARGET_VULKAN_VERSION = shaderc_env_version_vulkan_1_2;
	constexpr auto SHADER_OPTIMIZATION_LEVEL = shaderc_optimization_level_performance;
#ifdef _DEBUG
	constexpr bool SHADER_GENERATE_DEBUG_INFO = true;
#else
	constexpr bool SHADER_GENERATE_DEBUG_INFO = false;
#endif

	shaderc::CompileOptions create_compile_options( const std::filesystem::path& base_dir, const renderer::ShaderSource& source )
	{
		shaderc::CompileOptions options;
		// XXX: Technically we are using Vulkan 1.3 but the compiler emits incorrect LocalSizeId usage with mesh shaders
		options.SetTargetEnvironment( shaderc_target_env_vulkan, SHADER_TARGET_VULKAN_VERSION );
		options.SetOptimizationLevel( SHADER_OPTIMIZATION_LEVEL );
		options.SetIncluder( std::make_unique<ShaderIncluder>( base_dir ) );
		for ( const auto& define : source.defines )
		{
			options.AddMacroDefinition( define.key, define.value );
		}
		if ( SHADER_GENERATE_DEBUG_INFO )
		{
			options.SetGenerateDebugInfo();
		}

		return options;
	}

	constexpr uint64_t FNV1A_64_INIT = 14695981039346656037ull;
	[[nodiscard]] constexpr uint64_t fnv1a_64( std::span<const uint8_t> data, uint64_t hash = FNV1A_64_INIT )
	{
		for ( const auto b : data )
		{
			hash ^= b;
			hash *= 1099511628211ull; // FNV-1a 64-bit prime
		}
		return hash;
	}

	[[nodiscard]] constexpr uint64_t fnv1a_64( std::string_view sv, uint64_t hash = FNV1A_64_INIT )
	{
		const auto data = reinterpret_cast<const uint8_t*>( sv.data() );
		return fnv1a_64( std::span { data, data + sv.size() }, hash );
	}

	[[nodiscard]] constexpr uint64_t fnv1a_64( const char* s, uint64_t hash = FNV1A_64_INIT )
	{
		return fnv1a_64( std::string_view( s ), hash );
	}

	template <typename T>
	[[nodiscard]] constexpr uint64_t fnv1a_64( const T& v, uint64_t hash = FNV1A_64_INIT )
	{
		const auto data = reinterpret_cast<const uint8_t*>( &v );
		return fnv1a_64( std::span { data, data + sizeof( T ) }, hash );
	}

	// Cache files are this checksum of the SPIR-V followed by the SPIR-V
	[[nodiscard]] uint64_t spirv_checksum( std::span<const uint32_t> code )
	{
		const auto data = reinterpret_cast<const uint8_t*>( code.data() );
		return fnv1a_64( std::span { data, code.size_bytes() } );
	}
}

struct renderer::ShaderCompiler::Impl
{
	explicit Impl( std::filesystem::path dir )
		: base_dir( std::move( dir ) )
	{
	}

	shaderc::Compiler compiler;
	std::filesystem::path base_dir;
};

renderer::ShaderCompiler::ShaderCompiler( std::filesystem::path base_dir )
	: _impl( std::make_unique<Impl>( std::move( base_dir ) ) )
{
}

renderer::ShaderCompiler::~ShaderCompiler() = default;

const std::filesystem::path& renderer::ShaderCompiler::get_base_directory() const
{
	return _impl->base_dir;
}

std::expected<renderer::raii::ShaderCode, std::string> renderer::ShaderCompiler::compile( ShaderSource source ) const
{
	const auto path = _impl->base_dir / source.path;
	std::ifstream istream( path );
	if ( !istream )
	{
		return std::unexpected( std::format( "Couldn't open shader file '{}'", path.string() ) );
	}
	const std::string source_code { std::istreambuf_iterator<char>( istream ), std::istreambuf_iterator<char>() };
	return compile( std::move( source ), source_code );
}

std::expected<renderer::raii::ShaderCode, std::string> renderer::ShaderCompiler::compile( ShaderSource source,
																						  std::string_view source_code ) const
{
	PROFILER_SCOPE();
	const auto options = create_compile_options( _impl->base_dir, source );
	const auto result = _impl->compiler.CompileGlslToSpv( source_code.data(),
														  source_code.size(),
														  get_shader_kind( source.stage ),
														  source.path.c_str(),
														  options );

	if ( result.GetCompilationStatus() != shaderc_compilation_status_success )
	{
		return std::unexpected( result.GetErrorMessage() );
	}

	// Hiding away shaderc means we need to make a copy since it doesn't provide a way to take ownership of the data
	// If this proves to be a serious hindrance we could replace the vector with a type erased shaderc_compilation_result_t
	return raii::ShaderCode( std::move( source ), std::vector<uint32_t>( result.begin(), result.end() ) );
}

std::expected<renderer::ShaderSourceHash, std::string> renderer::ShaderCompiler::hash_source( const ShaderSource& source ) const
{
	PROFILER_SCOPE();

	const auto path = _impl->base_dir / source.path;
	std::ifstream istream( path );
	if ( !istream )
	{
		return std::unexpected( std::format( "Couldn't open shader file '{}'", path.string() ) );
	}
	const std::string source_code { std::istreambuf_iterator<char>( istream ), std::istreambuf_iterator<char>() };

	const auto options = create_compile_options( _impl->base_dir, source );

	const auto result = _impl->compiler.PreprocessGlsl( source_code.data(),
														source_code.size(),
														get_shader_kind( source.stage ),
														source.path.c_str(),
														options );

	if ( result.GetCompilationStatus() != shaderc_compilation_status_success )
	{
		return std::unexpected( result.GetErrorMessage() );
	}

	auto hash = fnv1a_64( GLSLANG_VERSION_MAJOR );
	hash = fnv1a_64( GLSLANG_VERSION_MINOR, hash );
	hash = fnv1a_64( GLSLANG_VERSION_PATCH, hash );
	hash = fnv1a_64( spvSoftwareVersionDetailsString(), hash );
	hash = fnv1a_64( SHADER_TARGET_VULKAN_VERSION, hash );
	hash = fnv1a_64( SHADER_OPTIMIZATION_LEVEL, hash );
	hash = fnv1a_64( SHADER_GENERATE_DEBUG_INFO, hash );
	hash = fnv1a_64( source.stage, hash );
	hash = fnv1a_64( { result.begin(), result.end() }, hash );

	return ShaderSourceHash { hash };
}

renderer::ShaderCache::ShaderCache( std::filesystem::path base_dir )
	: _base_dir( std::move( base_dir ) )
{
	std::error_code err { };
	std::filesystem::create_directories( _base_dir, err );
	if ( err )
	{
		throw Error( std::format( "Could not create shader cache directory: {}", err.message() ) );
	}
}

std::filesystem::path renderer::ShaderCache::get_path( ShaderSourceHash key ) const
{
	return _base_dir / std::format( "{:016X}.spirv", key.hash );
}

std::optional<renderer::raii::ShaderCode> renderer::ShaderCache::get( ShaderSource& source, ShaderSourceHash key ) const
{
	std::ifstream file( get_path( key ), std::ios::binary | std::ios::ios_base::ate );

	if ( !file )
	{
		return std::nullopt;
	}

	uint64_t checksum = 0;
	const auto file_size = file.tellg();
	if ( file_size % sizeof( uint32_t ) != 0 || file_size <= sizeof( checksum ) )
	{
		return std::nullopt;
	}

	const size_t code_size = static_cast<size_t>( file_size ) - sizeof( checksum );
	std::vector<uint32_t> blob( code_size / sizeof( uint32_t ) );
	file.seekg( 0, std::ios::beg );
	file.read( reinterpret_cast<char*>( &checksum ), sizeof( checksum ) );
	file.read( reinterpret_cast<char*>( blob.data() ), static_cast<std::streamsize>( code_size ) );
	if ( !file || spirv_checksum( blob ) != checksum )
	{
		return std::nullopt;
	}

	return raii::ShaderCode( std::move( source ), std::move( blob ) );
}

void renderer::ShaderCache::add( ShaderSourceHash key, const raii::ShaderCode& code ) const
{
	const auto path = get_path( key );
	auto tmp_path = path;
	tmp_path += ".tmp";

	std::ofstream file( tmp_path, std::ios::binary | std::ios::trunc );
	if ( !file )
	{
		return;
	}
	const uint64_t checksum = spirv_checksum( { code.get_data(), code.get_size() } );
	file.write( reinterpret_cast<const char*>( &checksum ), sizeof( checksum ) );
	file.write( reinterpret_cast<const char*>( code.get_data() ), code.get_size_bytes() );
	file.close();

	if ( file )
	{
		std::error_code err { };
		std::filesystem::remove( path, err );
		std::filesystem::rename( tmp_path, path, err );
		if ( err )
		{
			std::filesystem::remove( tmp_path, err );
		}
	}
	else
	{
		std::error_code ignored { };
		std::filesystem::remove( tmp_path, ignored );
	}
}
