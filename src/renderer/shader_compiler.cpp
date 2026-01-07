#include "shader_compiler.h"

#include <format>
#include <fstream>
#include <renderer/details/profiler.h>
#include <shaderc/shaderc.hpp>

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
			return {};
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

std::expected<renderer::raii::ShaderCode, std::string> renderer::ShaderCompiler::compile( ShaderStage stage,
																						  std::string_view filename ) const
{
	const auto path = _impl->base_dir / filename;
	std::ifstream istream( path );
	if ( !istream )
	{
		return std::unexpected( std::format( "Couldn't open shader file '{}'", path.string() ) );
	}
	const std::string source { std::istreambuf_iterator<char>( istream ), std::istreambuf_iterator<char>() };
	return compile( stage, source, path.string() );
}

std::expected<renderer::raii::ShaderCode, std::string>
renderer::ShaderCompiler::compile( ShaderStage stage, std::string_view source, std::string filename ) const
{
	OPTICK_EVENT();
	shaderc::CompileOptions options;
	// XXX: Technically we are using Vulkan 1.3 but the compiler emits incorrect LocalSizeId usage with mesh shaders
	options.SetTargetEnvironment( shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2 );
	options.SetOptimizationLevel( shaderc_optimization_level_performance );
	options.SetIncluder( std::make_unique<ShaderIncluder>( _impl->base_dir ) );
#ifdef _DEBUG
	options.SetGenerateDebugInfo();
#endif

	const auto result = _impl->compiler.CompileGlslToSpv( source.data(),
														  source.size(),
														  get_shader_kind( stage ),
														  filename.c_str(),
														  options );

	if ( result.GetCompilationStatus() != shaderc_compilation_status_success )
	{
		return std::unexpected( result.GetErrorMessage() );
	}

	// Hiding away shaderc means we need to make a copy since it doesn't provide a way to take ownership of the data
	// If this proves to be a serious hindrance we could replace the vector with a type erased shaderc_compilation_result_t
	return raii::ShaderCode( stage, std::vector<uint32_t>( result.begin(), result.end() ), std::move( filename ) );
}
