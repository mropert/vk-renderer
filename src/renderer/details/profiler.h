#pragma once

#ifdef USE_OPTICK
#include <optick.h>
#define OPTICK_THREAD_STATIC( x )   static thread_local OPTICK_THREAD( x )

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

#define OPTICK_THREAD_STATIC( ... )
#endif

#ifdef USE_TRACY
#include <optional>
#include <tracy/Tracy.hpp>
#define TRACY_SCOPE() ZoneScoped
#define TRACY_SCOPE_N( name) ZoneScopedN( name )

#define TRACY_SCOPE_PUSH( var, label )                                            \
	static constexpr tracy::SourceLocationData TracyConcat( __tracy_srcloc_, var ) \
		{ label, TracyFunction, TracyFile, (uint32_t)TracyLine, 0 };               \
	std::optional<tracy::ScopedZone> var( std::in_place, &TracyConcat( __tracy_srcloc_, var ) )
#define TRACY_SCOPE_POP( var ) var.reset()

struct TracyFrameScope
{
    ~TracyFrameScope() { tracy::Profiler::SendFrameMark( nullptr ); }
};

#define TRACY_FRAME() TracyFrameScope __tracy_frame_scope
#define TRACY_THREAD( name ) tracy::SetThreadName( name )

#else
#define TRACY_SCOPE()
#define TRACY_SCOPE_N( ... )
#define TRACY_SCOPE_PUSH( var, label )
#define TRACY_SCOPE_POP( var )
#define TRACY_FRAME()
#define TRACY_THREAD( name )
#endif


#define PROFILER_SCOPE() OPTICK_EVENT(); TRACY_SCOPE()
#define PROFILER_SCOPE_N( name ) OPTICK_EVENT( name ); TRACY_SCOPE_N( name )
#define PROFILER_SCOPE_PUSH( var, label ) OPTICK_PUSH( label ); TRACY_SCOPE_PUSH( var, label )
#define PROFILER_SCOPE_POP( var ) OPTICK_POP(); TRACY_SCOPE_POP( var )
#define PROFILER_SHUTDOWN() OPTICK_SHUTDOWN()
#define PROFILER_FRAME( name ) OPTICK_FRAME( name ); TRACY_FRAME()
#define PROFILER_THREAD( name ) OPTICK_THREAD( name ); TRACY_THREAD( name )
#define PROFILER_THREAD_STATIC( name ) OPTICK_THREAD_STATIC( name ); TRACY_THREAD( name )
#define PROFILER_GPU_EVENT( name ) OPTICK_GPU_EVENT( name )


#define PROFILER_START_CAPTURE() OPTICK_START_CAPTURE()
#define PROFILER_STOP_CAPTURE()  OPTICK_STOP_CAPTURE()
#define PROFILER_SAVE_CAPTURE( x ) OPTICK_SAVE_CAPTURE( x )
