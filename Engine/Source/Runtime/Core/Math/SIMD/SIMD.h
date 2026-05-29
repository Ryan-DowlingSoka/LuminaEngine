#pragma once

// Lumina::SIMD -- thin, explicit wrappers over x86 SIMD intrinsics for writing
// hand-vectorized hot loops. This is a low-level building block, NOT a
// replacement for the Lumina::Math vector/matrix types (TVec etc.); use it when
// processing arrays of data in bulk.
//
//   VFloat4 -- 4-lane f32 (SSE/SSE4.1, __m128)
//   VFloat8 -- 8-lane f32 (AVX, __m256)  -- the workhorse on the AVX baseline
//
// Each type supports operator +,-,*,/ and unary -, plus free functions:
//   Min Max Abs Sqrt Reciprocal[Fast] InvSqrt[Fast] Floor Ceil Round Truncate
//   MulAdd (FMA when the toolchain targets AVX2, else mul+add)
//   Cmp{Eq,Ne,Lt,Le,Gt,Ge} -> mask; And Or Xor AndNot Select MoveMask Any All None
//   HorizontalSum/Min/Max (+ Dot on VFloat4)
//
// Example -- a[i] = a[i]*s + b[i] over a float array (tail handled scalar):
//   using namespace Lumina::SIMD;
//   const VFloat8 S = VFloat8::Broadcast(s);
//   int i = 0;
//   for (; i + 8 <= N; i += 8)
//       MulAdd(VFloat8::Load(a + i), S, VFloat8::Load(b + i)).Store(a + i);
//   for (; i < N; ++i) a[i] = a[i]*s + b[i];
//
// Buffers passed to LoadAligned/StoreAligned must be SIMD::kAlignment-aligned.

#include "SIMDConfig.h"
#include "VFloat4.h"
#include "VFloat8.h"

namespace Lumina::SIMD
{
    // Out[i] = A[i]*(1-Alpha) + B[i]*Alpha over Count floats -- matches Math::Mix
    // exactly (under /arch:AVX, MulAdd is mul+add, so bit-identical to scalar).
    // Out may alias A or B (element-wise, no cross-lane dependency). Tail scalar.
    inline void LerpArray(float* Out, const float* A, const float* B, int Count, float Alpha)
    {
        const VFloat8 VAlpha    = VFloat8::Broadcast(Alpha);
        const VFloat8 VOneMinus = VFloat8::Broadcast(1.0f - Alpha);

        int i = 0;
        for (; i + 8 <= Count; i += 8)
        {
            const VFloat8 Va = VFloat8::Load(A + i);
            const VFloat8 Vb = VFloat8::Load(B + i);
            MulAdd(Va, VOneMinus, Vb * VAlpha).Store(Out + i);
        }

        const float OneMinus = 1.0f - Alpha;
        for (; i < Count; ++i)
        {
            Out[i] = A[i] * OneMinus + B[i] * Alpha;
        }
    }
}
