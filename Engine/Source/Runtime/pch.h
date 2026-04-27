#pragma once

// ===================
// Standard Library
// ===================
// Kept: small, used everywhere, or required by EASTL/glm/etc. transitively.
// Removed (moved to direct includes in their few users):
//   <iostream> <iomanip> <sstream> -- 0 uses anywhere.
//   <variant> <span> <bitset> <random> <stdexcept> -- <=7 uses, low fan-out.
//   <thread> <mutex> -- prefer Core/Threading primitives; drop from PCH.
//   <filesystem> -- ~14 files use it; they include it directly now.
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

// ===================
// EASTL
// ===================
#include <eastl/type_traits.h>
#include <eastl/random.h>
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
#include <eastl/atomic.h>
#include <eastl/numeric_limits.h>
#include <eastl/any.h>
#include <eastl/sort.h>
#include <eastl/memory.h>
#include <eastl/string.h>
#include <eastl/string_view.h>
#include <eastl/algorithm.h>
#include <eastl/functional.h>
#include <eastl/memory.h>
#include <eastl/unique_ptr.h>
#include <eastl/shared_ptr.h>
#include <eastl/weak_ptr.h>
#include <eastl/deque.h>
#include <eastl/list.h>
#include <eastl/stack.h>
#include <eastl/queue.h>
#include <eastl/bitset.h>


// ===================
// Third-Party Libraries
// ===================
// Kept here:
//   glm     - vec/mat types are used in nearly every TU.
//   entt    - 48+ files reach into entt:: directly via component / system
//             headers; pulling it out of the PCH would just shift the parse
//             cost from "once" to "48+ times".
//   spdlog  - logging hits every TU.
//   xxhash  - small, heavily used.
// Removed: <Jolt/Jolt.h> -- only ~7 files use Jolt; they include it directly.
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <spdlog/spdlog.h>
#include <xxhash.h>
