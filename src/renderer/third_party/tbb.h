#pragma once

// TBB includes <Windows.h> for some reason
// There are multiple PRs open to fix this, none of them merged
// * https://github.com/uxlfoundation/oneTBB/pull/576
// * https://github.com/uxlfoundation/oneTBB/pull/1932
// In the meantime we have to do this
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <tbb/tbb.h>
