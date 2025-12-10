# vk-renderer

A Vulkan renderer abstraction in C++23.

Everyone has to write a renderer at least once to get how it works. So here's my entry. This is very WIP and can change without notice.
I use C++ Modules and Exceptions, deal with it.

## Overview

Check examples to get an idea of how this work.

## Building

This requires Vulkan, Vulkan Memory Allocator (VMA), shaderc, SDL3 and TBB. Easiest way to make it work is to use vcpkg:

```
vcpkg install vulkan vulkan-validationlayers vulkan-memory-allocator shaderc sdl3 tbb
```

This library also uses VkBootstrap but the source is included under `third_party` for convenience as it's not on vckpg.
