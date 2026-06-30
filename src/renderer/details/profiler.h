#pragma once

#ifdef USE_OPTICK
#include <optick.h>
#else
#define OPTICK_EVENT( ... )
#define OPTICK_GPU_EVENT( ... )
#define OPTICK_THREAD( ... )
#define OPTICK_FRAME( ... )
#define OPTICK_PUSH( ... )
#define OPTICK_POP( ... )
#define OPTICK_SHUTDOWN( ... )
#define OPTICK_START_CAPTURE( ... )
#define OPTICK_STOP_CAPTURE( ... )
#define OPTICK_SAVE_CAPTURE( ... )
#endif
