#pragma once

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

namespace renderer
{
	class BindlessManager;
	class Device;

	using PipelineHandle = uint32_t;

	class PipelineManager
	{
		struct Item
		{
			raii::Pipeline pipeline;
			std::vector<ShaderSource> sources;
			std::vector<std::filesystem::file_time_type> last_writes;
		};

		struct RebuiltItem
		{
			std::variant<raii::Pipeline, Error> result;
			std::vector<std::filesystem::file_time_type> last_writes;
		};

	public:
		PipelineManager( Device& device, std::filesystem::path shader_dir, const BindlessManager& bindless_manager );

		// Creates and return new pipeline. Safe to call from multiple threads at once.
		PipelineHandle add( const Pipeline::Desc& desc, std::initializer_list<ShaderSource> shaders );
		// Updates any outdated pipeline from the async thread if avaible. Does not wait for pending updates.
		// Call each frame before rendering to get updated shaders.
		void update();
		// Returns managed pipeline for binding to a command buffer.
		// Not synchronized to avoid mutexes in render code, do not call while add() calls are in flight.
		Pipeline get( PipelineHandle pipeline ) const;

	private:
		raii::Pipeline make( const Pipeline::Desc& desc, std::span<const ShaderSource> sources ) const;
		void rebuild_job();

		Device* _device;
		const BindlessManager* _bindless_manager;
		ShaderCompiler _compiler;
		std::vector<Item> _items;
		// Recursive mtx has higher perf on windows, blame MSVC runtime
		std::recursive_mutex _mtx;
		std::unordered_map<PipelineHandle, RebuiltItem> _updated_items;
		std::jthread _rebuild_thread;
	};
}
