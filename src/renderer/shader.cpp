#include "shader.h"

#include <shaderc/shaderc.hpp>


struct renderer::ShaderCompiler::Impl
{
	shaderc::Compiler compiler;
};

renderer::ShaderCompiler::ShaderCompiler()
	: _impl( std::make_unique<Impl>() )
{
}

renderer::ShaderCompiler::~ShaderCompiler() = default;

std::expected<renderer::raii::ShaderCode, std::string>
renderer::ShaderCompiler::compile( ShaderStage stage, std::string_view source, std::string filename ) const
{
	shaderc::CompileOptions options;
	options.SetOptimizationLevel( shaderc_optimization_level_performance );

	const auto result = _impl->compiler.CompileGlslToSpv( source.data(),
														  source.size(),
														  stage == ShaderStage::VERTEX ? shaderc_glsl_vertex_shader
																					   : shaderc_glsl_fragment_shader,
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
