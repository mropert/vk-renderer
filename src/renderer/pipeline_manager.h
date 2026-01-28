#pragma once

#include <condition_variable>
#include <expected>
#include <filesystem>
#include <mutex>
#include <renderer/common.h>
#include <renderer/pipeline.h>
#include <renderer/shader.h>
#include <renderer/shader_compiler.h>
#include <span>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

namespace renderer
{
	class BindlessManagerBase;
	class Device;

	using PipelineHandle = uint32_t;

	class PipelineManager
	{
		struct Shader
		{
			raii::ShaderCode code;
			std::filesystem::file_time_type last_write;
		};

		struct Item
		{
			std::variant<Pipeline::Desc, raii::Pipeline> pipeline;
			std::vector<int> sources;
		};

		using MakePipelineResult = std::expected<raii::Pipeline, Error>;

	public:
		PipelineManager( Device& device, std::filesystem::path shader_dir, const BindlessManagerBase& bindless_manager );

		// Creates and return new pipeline. Safe to call from multiple threads at once.
		PipelineHandle add( Pipeline::Desc desc, std::initializer_list<ShaderSource> shaders );
		// Updates any outdated pipeline from the async thread if avaible. Does not wait for pending updates.
		// Call each frame before rendering to get updated shaders.
		void update();
		// Returns managed pipeline for binding to a command buffer.
		// Not synchronized to avoid mutexes in render code, do not call while add() calls are in flight.
		Pipeline get( PipelineHandle pipeline ) const;
		// Wait until all added pipelines have been created (or throw an exception if some couldn't be built)
		// Subsequent rebuilds in flight will not block
		void wait_ready();

	private:
		MakePipelineResult make( const Pipeline::Desc& desc, std::span<const raii::ShaderCode*> shaders ) const;
		std::vector<int> rebuild_outdated_shaders();
		void rebuild_job();

		Device* _device;
		const BindlessManagerBase* _bindless_manager;
		ShaderCompiler _compiler;
		std::vector<Shader> _shaders;
		std::vector<Item> _items;
		// Recursive mtx has higher perf on windows, blame MSVC runtime
		std::recursive_mutex _mtx;
		std::condition_variable_any _ready_signal;
		int _available_pipelines = 0;
		std::vector<Error> _pending_errors;
		std::unordered_map<PipelineHandle, MakePipelineResult> _updated_items;
		std::jthread _rebuild_thread;
	};
}
