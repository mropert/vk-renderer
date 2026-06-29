#define VMA_IMPLEMENTATION
// vcpkg installs under a different path in Windows vs other OSs...
#ifdef _WIN32
#include <vma/vk_mem_alloc.h>
#else
#include <vk_mem_alloc.h>
#endif
