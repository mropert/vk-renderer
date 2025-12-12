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