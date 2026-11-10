# vk-renderer

A Vulkan renderer abstraction in C++23.

Everyone has to write a renderer at least once to get how it works. So here's my entry. This is very WIP and can change without notice.
I use C++ Modules and Exceptions, deal with it.

## Overview

Check examples to get an idea of how this work.

## Features

* No descriptor management! Bindless textures and buffers only.
* Background pipeline hot reload when source code has changed

Stuff is being added iteratively as I get a use case for them. This might lead to API refactoring/rewriting.

## Hardware requirements

The API is built on top of Vulkan 1.3 core, which requires the following:
* **NVIDIA**: Maxwell or newer, driver 511.65+ on Windows, 510.47.03+ on Linux.
* **AMD**: GCN 4 (Polaris) or newer on Windows; GCN 1 or newer on Linux with RADV (Mesa 22.0+).
* **Apple**: M1 or newer with MoltenVK 1.3.0+ (April 2025).

The instance also requires `VK_EXT_debug_utils`, for `renderer::CommandBuffer::debug_label`. The Khronos loader implements it
itself, so it's available on desktop platforms whatever the driver.

The following extensions and features are used when available but will not produce hard errors when creating a `Device` on
unsupported hardware:
* `pipelineStatisticsQuery`: `renderer::StatisticsQuery` is only available if `Device::Properties::statistics_support` is true.
    Timestamp queries are available regardless.
* `VK_EXT_mesh_shader`: `TASK` and `MESH` `ShaderStage` are only available if `Device::Properties::mesh_shader_support` is true.
    Likewise calling `CommandBuffer::draw_mesh_tasks()` and `CommandBuffer::draw_mesh_tasks_indirect()` will be undefined behaviour.
* `drawIndirectCount`: Calling the `CommandBuffer::draw_indexed_indirect()` overload with a count buffer will lead to undefined behaviour
    if `Device::Properties::draw_indirect_count_support` is false.
* `samplerFilterMinmax`: Only `renderer::Sampler::ReductionMode::AVERAGE` will be supported if `Device::Properties::minmax_filter_support`
    is false. Using `renderer::BindlessManager::LINEAR_MIN_SAMPLER` without `samplerFilterMinmax` support will yield a `LINEAR_SAMPLER`.

## Building

This requires Vulkan, Vulkan Memory Allocator (VMA), shaderc, SDL3 and TBB. Easiest way to make it work is to use vcpkg:

```
vcpkg install vulkan vulkan-validationlayers vulkan-memory-allocator shaderc sdl3 tbb
```

This library also uses VkBootstrap but the source is included under `third_party` for convenience as it's not on vckpg.

## Profiling

The library has instrumentation profiling support using Optick (https://github.com/bombomby/optick).

If an `Optick` target is found in CMake's context, CPU and GPU event scopes will be added for most non-trivial operations.

As mentioned in examples, Optick has no way to unregister a GPU once set. You will need to call `OPTICK_SHUTDOWN()` before
running the `renderer::Device` destructors.