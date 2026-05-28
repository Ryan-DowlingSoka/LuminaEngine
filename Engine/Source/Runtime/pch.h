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
// Trimmed:
//   <eastl/any.h>, <eastl/random.h>          -- rarely used, expensive templates.
//   <eastl/list.h> <eastl/stack.h>           -- Containers/Array.h pulls these
//   <eastl/queue.h> <eastl/deque.h>             when callers actually need them
//   <eastl/bitset.h>                            (75 headers include Array.h).
//   <eastl/numeric_limits.h>                 -- covered by <limits>.
//   duplicate <eastl/memory.h>               -- was listed twice.
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
// variant.h is expensive template code, but Core/Variant/Variant.h pulls it
// into 166 of 313 TUs -- amortize the parse through the PCH.
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


// ===================
// Third-Party Libraries
// ===================
// Kept here:
//   glm     - vec/mat types are used in nearly every TU.
//   entt    - 48+ files reach into entt:: directly via component / system
//             headers; pulling it out of the PCH would just shift the parse
//             cost from "once" to "48+ times".
//   xxhash  - small, heavily used.
// Removed:
//   <spdlog/spdlog.h> -- Log/Log.h already includes spdlog and the 32 TUs that
//                        log already go through Log.h. The fmt template
//                        expansion was paying for every TU regardless.
//   <Jolt/Jolt.h> -- only ~7 files use Jolt; they include it directly.
#include <entt/entt.hpp>
#include <xxhash.h>

// Lumina vector + quaternion types. Like glm, used in nearly every TU; the PCH
// also makes the real FVector*/FQuat aliases visible to the reflection-unity TU
// that compiles their manually-reflected registration (Core/Object/ManualReflectTypes.h).
#include "Core/Math/Vector/Vector.h"
#include "Core/Math/Quat/Quat.h"
#include "Core/Math/Matrix/Matrix.h"
#include "Core/Math/Matrix/MatrixMath.h"
#include "Core/Math/Scalar.h"
#include "Core/Math/Packing.h"
#include "Core/Math/MathString.h"
