#pragma once

// Win32 hygiene
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NODRAWTEXT
#define NODRAWTEXT
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef NORESOURCE
#define NORESOURCE
#endif


// Standard library: only headers that are small, ubiquitous, or pulled transitively by EASTL.
#include <memory>
#include <string>
#include <utility>
#include <cstdint>
#include <cstddef>
#include <iterator>
#include <tuple>
#include <vector>
#include <array>
#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>
#include <algorithm>
#include <functional>
#include <cmath>
#include <numeric>
#include <chrono>
#include <atomic>
#include <type_traits>
#include <optional>
#include <limits>
#include <cassert>

#include <eastl/type_traits.h>
#include <eastl/utility.h>
#include <eastl/array.h>
#include <eastl/vector.h>
#include <eastl/map.h>
#include <eastl/unordered_map.h>
#include <eastl/set.h>
#include <eastl/unordered_set.h>
#include <eastl/hash_set.h>
#include <eastl/hash_map.h>
#include <eastl/fixed_hash_set.h>
#include <eastl/fixed_hash_map.h>
#include <eastl/fixed_vector.h>
#include <eastl/fixed_string.h>
// Expensive, but Core/Variant pulls it into ~half of all TUs; amortize the parse here.
#include <eastl/variant.h>
#include <eastl/atomic.h>
#include <eastl/sort.h>
#include <eastl/memory.h>
#include <eastl/string.h>
#include <eastl/string_view.h>
#include <eastl/algorithm.h>
#include <eastl/functional.h>
#include <eastl/unique_ptr.h>
#include <eastl/shared_ptr.h>
#include <eastl/weak_ptr.h>

// entt is reached directly by 48+ files; keeping it here parses it once instead of per-TU.
#include <entt/entt.hpp>
#include <xxhash.h>

// Math types used nearly everywhere; headers also carry REFLECTION_PARSER stubs the reflector walks.
#include "Core/Math/Vector/Vector.h"
#include "Core/Math/Quat/Quat.h"
#include "Core/Math/Matrix/Matrix.h"
#include "Core/Math/Matrix/MatrixMath.h"
#include "Core/Math/Scalar.h"
#include "Core/Math/Packing.h"
#include "Core/Math/MathString.h"
