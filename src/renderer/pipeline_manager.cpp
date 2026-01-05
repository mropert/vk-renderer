#include "pipeline_manager.h"

#include <renderer/details/profiler.h>
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
			  OPTICK_THREAD( "pipeline_rebuild" );
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

renderer::PipelineHandle renderer::PipelineManager::add( const Pipeline::Desc& desc, std::initializer_list<ShaderSource> shaders )
{
	const auto& base_dir = _compiler.get_base_directory();
	std::vector<ShaderSource> sources = shaders;
	std::vector<std::filesystem::file_time_type> last_writes;
	last_writes.reserve( sources.size() );
	for ( const auto source : sources )
	{
		last_writes.emplace_back( std::filesystem::last_write_time( base_dir / source.path ) );
	}

	auto pipeline = make( desc, sources );
	std::scoped_lock lock( _mtx );
	auto handle = static_cast<PipelineHandle>( _items.size() );
	_items.emplace_back( std::move( pipeline ), std::move( sources ), std::move( last_writes ) );
	return handle;
}

void renderer::PipelineManager::update()
{
	OPTICK_EVENT();
	// FIXME: to avoid blocking on a rendering thread we should probably do a try_lock() here
	// and bail if it fails to acquire it (and try again next frame).
	// But so far this hasn't been an issue.
	std::scoped_lock lock( _mtx );
	for ( auto& [ handle, item ] : _updated_items )
	{
		// Preserve working shaders if the new ones failed to compile/link, but update timestamps so we don't keep rebuilding
		if ( auto pipeline = std::get_if<raii::Pipeline>( &item.result ) )
		{
			_device->queue_deletion( std::move( _items[ handle ].pipeline ) );
			_items[ handle ].pipeline = std::move( *pipeline );
		}
		_items[ handle ].last_writes = std::move( item.last_writes );
	}
	_updated_items.clear();
}

renderer::Pipeline renderer::PipelineManager::get( PipelineHandle pipeline ) const
{
	return _items[ pipeline ].pipeline;
}

renderer::raii::Pipeline renderer::PipelineManager::make( const Pipeline::Desc& desc, std::span<const ShaderSource> sources ) const
{
	OPTICK_EVENT();
	std::vector<renderer::raii::ShaderCode> shaders( sources.size() );
	tbb::parallel_for( 0zu,
					   sources.size(),
					   [ & ]( size_t index )
					   { shaders[ index ] = compile_shader( _compiler, sources[ index ].path, sources[ index ].stage ); } );

	std::vector<renderer::ShaderCode> shader_descs;
	std::copy( begin( shaders ), end( shaders ), std::back_inserter( shader_descs ) );

	return _device->create_pipeline( desc, shader_descs, *_bindless_manager );
}

namespace
{
	struct RebuildRequest
	{
		renderer::PipelineHandle handle;
		renderer::Pipeline::Desc desc;
		std::vector<renderer::ShaderSource> sources;
		std::vector<std::filesystem::file_time_type> last_writes;
		std::variant<renderer::raii::Pipeline, renderer::Error> result;
	};
}

void renderer::PipelineManager::rebuild_job()
{
	OPTICK_EVENT();
	std::vector<RebuildRequest> to_rebuild;
	{
		std::scoped_lock lock( _mtx );
		// FIXME: if shaders share sources we will check their last write time twice (or more)
		// We should instead have a map of a filename -> set<PipelineHandle>.
		// Also, instead of polling the filesystem every 1s we should use a filewatcher,
		// but std::filesystem doesn't provide one and this isn't worth writing our own at the moment.
		for ( PipelineHandle i = 0; i < _items.size(); ++i )
		{
			// Check if we somehow don't already have rebuilt the pipeline but update() hasn't been called yet
			const auto it = _updated_items.find( i );
			const auto& timestamps = it != end( _updated_items ) ? it->second.last_writes : _items[ i ].last_writes;
			const auto& base_dir = _compiler.get_base_directory();
			std::vector<std::filesystem::file_time_type> last_writes( timestamps.size() );
			bool needs_rebuild = false;
			for ( int source = 0; source < _items[ i ].sources.size(); ++source )
			{
				last_writes[ source ] = std::filesystem::last_write_time( base_dir / _items[ i ].sources[ source ].path );
				needs_rebuild = needs_rebuild || last_writes[ source ] > timestamps[ source ];
			}
			if ( needs_rebuild )
			{
				to_rebuild.push_back( RebuildRequest { .handle = i,
													   .desc = _items[ i ].pipeline.get_desc(),
													   .sources = _items[ i ].sources,
													   .last_writes = std::move( last_writes ) } );
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
			item.result = make( item.desc, item.sources );
		}
		catch ( Error e )
		{
			item.result = std::move( e );
		}
	}
	std::scoped_lock lock( _mtx );
	for ( auto& item : to_rebuild )
	{
		_updated_items.insert_or_assign( item.handle,
										 RebuiltItem { .result = std::move( item.result ), .last_writes = std::move( item.last_writes ) } );
	}
}
