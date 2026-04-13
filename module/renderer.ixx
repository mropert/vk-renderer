module;

#include <renderer/includes.h>

export module renderer;

// Hack to force MSVC to keep <compare>
// https://www.reddit.com/r/cpp/comments/1b0zem7/comment/ksc8ix8/
namespace msvc_modules_hack
{
	export inline std::tuple<std::partial_ordering, std::weak_ordering, std::strong_ordering> dummy()
	{
		return {};
	}
}

// Don't mangle as a module for backward compatibility with non modules includes
extern "C++"
{
	export {
#ifdef _MSC_VER
#pragma warning( push )
#pragma warning( disable : 5244 )
#endif

#include "renderer/renderer.h"

#ifdef _MSC_VER
#pragma warning( pop )
#endif
	}
}
