#pragma once

#ifdef USE_OPTICK
#include <optick.h>
#else
#define OPTICK_EVENT( ... )
#define OPTICK_THREAD( THREAD_NAME )
#endif
