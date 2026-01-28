#include "pipeline_manager.h"

#include <renderer/details/profiler.h>
#include <renderer/device.h>
#include <renderer/third_party/tbb.h>

namespace
{
	std::expected<renderer::raii::ShaderCode, renderer::Error> compile_shader( const renderer::ShaderCompiler& compiler,
																			   renderer::ShaderSource source )
	{
		auto compilation_result = compiler.compile( std::move( source ) );
		if ( !compilation_result )
		{
			return std::unexpected( renderer::Error( "Shader compilation failed: " + compilation_result.error() ) );
		}
		return std::move( compilation_result.value() );
	}

	// XXX: TBB seems to creates a different scheduler/arena for jthreads
	// We discovered this because we had to set a different observer instance to see the TBB workers used by the bindless manager

	struct tbb_observer final : public tbb::task_scheduler_observer
	{
		void on_scheduler_entry( bool is_worker ) override
		{
			if ( is_worker )
			{
				static thread_local OPTICK_THREAD( "TBB Worker" );
			}
		}
	};
}

renderer::PipelineManager::PipelineManager( Device& device, std::filesystem::path shader_dir, const BindlessManagerBase& bindless_manager )
	: _device( &device )
	, _bindless_manager( &bindless_manager )
	, _compiler( std::move( shader_dir ) )
	, _rebuild_thread(
		  [ & ]( std::stop_token tok )
		  {
			  OPTICK_THREAD( "pipeline_rebuild" );
			  tbb_observer observer;
			  observer.observe();
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

renderer::PipelineHandle renderer::PipelineManager::add( Pipeline::Desc desc, std::initializer_list<ShaderSource> sources )
{
	std::unique_lock lock( _mtx );
	auto handle = static_cast<PipelineHandle>( _items.size() );
	auto& entry = _items.emplace_back( std::move( desc ) );
	entry.sources.reserve( sources.size() );
	for ( const auto& source : sources )
	{
		// Linear find ain't great but for our amount of shaders it's fine at the moment
		auto it = std::find_if( begin( _shaders ),
								end( _shaders ),
								[ &source ]( const auto& shader ) { return shader.code.get_source() == source; } );
		if ( it == end( _shaders ) )
		{
			_shaders.emplace_back( raii::ShaderCode( source, {} ) );
			it = end( _shaders ) - 1;
		}
		entry.sources.push_back( std::distance( begin( _shaders ), it ) );
	}
	return handle;
}

void renderer::PipelineManager::update()
{
	OPTICK_EVENT();
	// FIXME: to avoid blocking on a rendering thread we should probably do a try_lock() here
	// and bail if it fails to acquire it (and try again next frame).
	// But so far this hasn't been an issue.
	std::unique_lock lock( _mtx );
	for ( auto& [ handle, result ] : _updated_items )
	{
		// Preserve working shaders if the new ones failed to compile/link
		if ( result )
		{
			_device->queue_deletion( std::move( std::get<raii::Pipeline>( _items[ handle ].pipeline ) ) );
			_items[ handle ].pipeline = std::move( result.value() );
		}
	}
	_updated_items.clear();
}

renderer::Pipeline renderer::PipelineManager::get( PipelineHandle pipeline ) const
{
	return std::get<raii::Pipeline>( _items[ pipeline ].pipeline );
}

void renderer::PipelineManager::wait_ready()
{
	OPTICK_EVENT();
	std::unique_lock lock( _mtx );
	while ( _pending_errors.empty() && _available_pipelines != _items.size() )
	{
		_ready_signal.wait( lock );
	}
	if ( !_pending_errors.empty() )
	{
		throw _pending_errors.front();
	}
}

renderer::PipelineManager::MakePipelineResult renderer::PipelineManager::make( const Pipeline::Desc& desc,
																			   std::span<const raii::ShaderCode*> shaders ) const
{
	OPTICK_EVENT();
	try
	{
		if ( shaders.size() == 1 && shaders[ 0 ]->get_source().stage == ShaderStage::COMPUTE )
		{
			return _device->create_compute_pipeline( desc, *shaders[ 0 ], *_bindless_manager );
		}
		else
		{
			return _device->create_graphics_pipeline( desc, shaders, *_bindless_manager );
		}
	}
	catch ( Error e )
	{
		return std::unexpected( std::move( e ) );
	}
}

std::vector<int> renderer::PipelineManager::rebuild_outdated_shaders()
{
	OPTICK_EVENT();
	struct RebuildRequest
	{
		int index;
		ShaderSource source;
		std::filesystem::file_time_type last_write;
		std::expected<raii::ShaderCode, Error> result;
	};

	std::vector<RebuildRequest> to_rebuild;

	{
		std::unique_lock lock( _mtx );
		// FIXME: we do not detect writes to includes, only the top level source file
		// Also, instead of polling the filesystem every 1s we should use a filewatcher,
		// but std::filesystem doesn't provide one and this isn't worth writing our own at the moment.
		for ( int i = 0; i < _shaders.size(); ++i )
		{
			const auto& base_dir = _compiler.get_base_directory();
			const auto timestamp = std::filesystem::last_write_time( base_dir / _shaders[ i ].code.get_source().path );
			if ( timestamp > _shaders[ i ].last_write )
			{
				to_rebuild.emplace_back( i, _shaders[ i ].code.get_source(), timestamp );
			}
		}
	}
	if ( to_rebuild.empty() )
	{
		return {};
	}

	tbb::parallel_for( 0zu,
					   to_rebuild.size(),
					   [ & ]( size_t index )
					   { to_rebuild[ index ].result = compile_shader( _compiler, std::move( to_rebuild[ index ].source ) ); } );

	std::vector<int> rebuilt;
	rebuilt.reserve( to_rebuild.size() );
	std::unique_lock lock( _mtx );
	for ( auto& item : to_rebuild )
	{
		if ( item.result )
		{
			_shaders[ item.index ].code = std::move( item.result.value() );
			_shaders[ item.index ].last_write = item.last_write;
			rebuilt.emplace_back( item.index );
		}
		else if ( _shaders[ item.index ].code.get_size() == 0 )
		{
			// Propagate the error if first time compile of a shader fails
			_pending_errors.push_back( std::move( item.result.error() ) );
		}
		else
		{
			// Swallow the error but update the timestamp to make sure we don't keep rebuilding a broken shader
			_shaders[ item.index ].last_write = item.last_write;
		}
	}
	return rebuilt;
}

void renderer::PipelineManager::rebuild_job()
{
	OPTICK_EVENT();
	const auto rebuilt_shaders = rebuild_outdated_shaders();
	std::vector<const raii::ShaderCode*> shaders;

	std::unique_lock lock( _mtx );
	for ( PipelineHandle i = 0; i < _items.size(); ++i )
	{
		int available = 0;
		int rebuilt = 0;
		for ( const auto source : _items[ i ].sources )
		{
			if ( _shaders[ source ].code.get_size() != 0 )
			{
				++available;
			}
			if ( std::find( begin( rebuilt_shaders ), end( rebuilt_shaders ), source ) != end( rebuilt_shaders ) )
			{
				++rebuilt;
			}
		}
		const auto unbuilt_desc = std::get_if<Pipeline::Desc>( &_items[ i ].pipeline );
		if ( available == _items[ i ].sources.size() && ( unbuilt_desc || rebuilt > 0 ) )
		{
			shaders.clear();
			for ( const auto source : _items[ i ].sources )
			{
				shaders.push_back( &_shaders[ source ].code );
			}
			if ( unbuilt_desc )
			{
				auto res = make( *unbuilt_desc, shaders );
				if ( res )
				{
					// Immediately assign the pipeline, by definition it can't be in use just yet
					_items[ i ].pipeline = std::move( res.value() );
					++_available_pipelines;
				}
				else
				{
					_pending_errors.push_back( std::move( res.error() ) );
					_updated_items.insert_or_assign( i, std::move( res ) );
				}
			}
			else
			{
				_updated_items.insert_or_assign( i, make( std::get<raii::Pipeline>( _items[ i ].pipeline ).get_desc(), shaders ) );
			}
		}
	}
	const bool signal_ready = !_pending_errors.empty() || _available_pipelines == _items.size();
	lock.unlock();

	if ( signal_ready )
	{
		_ready_signal.notify_one();
	}
}
