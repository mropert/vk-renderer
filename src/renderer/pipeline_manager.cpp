#include "pipeline_manager.h"

#include <renderer/device.h>
#include <tbb/tbb.h>

namespace
{
	renderer::raii::ShaderCode
	compile_shader( const renderer::ShaderCompiler& compiler, std::string_view path, renderer::ShaderStage stage )
	{
		auto compilation_result = compiler.compile( stage, path );
		if ( !compilation_result )
		{
			throw renderer::Error( "Shader compilation failed: " + compilation_result.error() );
		}
		return std::move( compilation_result.value() );
	}
}

renderer::PipelineManager::PipelineManager( Device& device, std::filesystem::path shader_dir, const BindlessManager& bindless_manager )
	: _device( &device )
	, _bindless_manager( &bindless_manager )
	, _compiler( std::move( shader_dir ) )
	, _rebuild_thread(
		  [ & ]( std::stop_token tok )
		  {
			  while ( !tok.stop_requested() )
			  {
				  rebuild_job();
				  if ( !tok.stop_requested() )
				  {
					  using namespace std::chrono_literals;
					  std::this_thread::sleep_for( 1s );
				  }
			  }
		  } )
{
}

renderer::PipelineHandle renderer::PipelineManager::add( const Pipeline::Desc& desc, std::string vs_path, std::string fs_path )
{
	const auto& base_dir = _compiler.get_base_directory();
	const auto vs_timestamp = std::filesystem::last_write_time( base_dir / vs_path );
	const auto fs_timestamp = std::filesystem::last_write_time( base_dir / fs_path );
	auto pipeline = make( desc, vs_path, fs_path );
	std::scoped_lock lock( _mtx );
	auto handle = static_cast<PipelineHandle>( _items.size() );
	_items.emplace_back( std::move( pipeline ), std::move( vs_path ), std::move( fs_path ), vs_timestamp, fs_timestamp );
	return handle;
}

void renderer::PipelineManager::update()
{
	std::scoped_lock lock( _mtx );
	for ( auto& [ handle, item ] : _updated_items )
	{
		// Preserve working shaders if the new ones failed to compile/link, but update timestamps so we don't keep rebuilding
		if ( auto pipeline = std::get_if<raii::Pipeline>( &item.result ) )
		{
			_device->queue_deletion( std::move( _items[ handle ].pipeline ) );
			_items[ handle ].pipeline = std::move( *pipeline );
		}
		_items[ handle ].last_vs_write = item.last_vs_write;
		_items[ handle ].last_fs_write = item.last_fs_write;
	}
	_updated_items.clear();
}

renderer::Pipeline renderer::PipelineManager::get( PipelineHandle pipeline ) const
{
	return _items[ pipeline ].pipeline;
}

renderer::raii::Pipeline
renderer::PipelineManager::make( const Pipeline::Desc& desc, std::string_view vs_path, std::string_view fs_path ) const
{
	renderer::raii::ShaderCode vertex_shader;
	renderer::raii::ShaderCode fragment_shader;
	tbb::parallel_invoke( [ & ] { vertex_shader = compile_shader( _compiler, vs_path, renderer::ShaderStage::VERTEX ); },
						  [ & ]
						  {
							  fragment_shader = compile_shader( _compiler, fs_path, renderer::ShaderStage::FRAGMENT );
							  ;
						  } );

	return _device->create_pipeline( desc, vertex_shader, fragment_shader, *_bindless_manager );
}

namespace
{
	struct RebuildRequest
	{
		renderer::PipelineHandle handle;
		renderer::Pipeline::Desc desc;
		std::string vs_path;
		std::string fs_path;
		std::filesystem::file_time_type last_vs_write;
		std::filesystem::file_time_type last_fs_write;
		std::variant<renderer::raii::Pipeline, renderer::Error> result;
	};
}

void renderer::PipelineManager::rebuild_job()
{
	std::vector<RebuildRequest> to_rebuild;
	{
		std::scoped_lock lock( _mtx );
		for ( PipelineHandle i = 0; i < _items.size(); ++i )
		{
			// Check if we somehow don't already have rebuilt the pipeline but update() hasn't been called yet
			const auto it = _updated_items.find( i );
			const auto vs_timestamp = it != end( _updated_items ) ? it->second.last_vs_write : _items[ i ].last_vs_write;
			const auto fs_timestamp = it != end( _updated_items ) ? it->second.last_fs_write : _items[ i ].last_fs_write;
			const auto& base_dir = _compiler.get_base_directory();
			const auto current_vs_timestamp = std::filesystem::last_write_time( base_dir / _items[ i ].vs_path );
			const auto current_fs_timestamp = std::filesystem::last_write_time( base_dir / _items[ i ].fs_path );
			if ( current_vs_timestamp > vs_timestamp || current_fs_timestamp > fs_timestamp )
			{
				to_rebuild.push_back( RebuildRequest { .handle = i,
													   .desc = _items[ i ].pipeline.get_desc(),
													   .vs_path = _items[ i ].vs_path,
													   .fs_path = _items[ i ].fs_path,
													   .last_vs_write = current_vs_timestamp,
													   .last_fs_write = current_fs_timestamp } );
			}
		}
	}
	if ( to_rebuild.empty() )
	{
		return;
	}

	for ( auto& item : to_rebuild )
	{
		try
		{
			item.result = make( item.desc, item.vs_path, item.fs_path );
		}
		catch ( Error e )
		{
			item.result = std::move( e );
		}
	}
	std::scoped_lock lock( _mtx );
	for ( auto& item : to_rebuild )
	{
		_updated_items.insert_or_assign(
			item.handle,
			RebuiltItem { .result = std::move( item.result ), .last_vs_write = item.last_vs_write, .last_fs_write = item.last_fs_write } );
	}
}
