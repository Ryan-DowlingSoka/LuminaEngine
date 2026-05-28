#pragma once

#include "Core/Math/Vector/Vector.h"
#include <type_traits>

// Lumina matrices. COLUMN-MAJOR storage (C columns, each a TVec<T,R>), indexed
// m[col][row], so they upload to the GPU byte-for-byte. Left-handed / zero-to-one
// depth only matter for the projection builders (perspective/ortho/lookAt), which
// live in the math function library, not here. This header is the type + its core
// algebra.
//
// Not reflected (no stubs in ManualReflectTypes.h).

namespace Lumina
{
    // C columns, R rows -> mathematically an R-by-C matrix. m[c] is column c.
    template<typename T, int C, int R>
    struct TMat
    {
        static_assert(C > 0 && R > 0, "TMat requires positive dimensions");

        using ColumnType = TVec<T, R>;
        using ScalarType = T;
        static constexpr int Columns = C;
        static constexpr int Rows    = R;

        ColumnType Cols[C];

        // Trivial default ctor: uninitialized. Use TMat(1) or
        // TMat::Identity() for the identity matrix.
        TMat() = default;

        explicit constexpr TMat(T Diagonal) : Cols{}
        {
            constexpr int N = C < R ? C : R;
            for (int i = 0; i < N; ++i)
            {
                Cols[i][i] = Diagonal;
            }
        }

        // From C column vectors: TMat(col0, col1, ...).
        template<typename... Cs>
            requires (sizeof...(Cs) == C && C >= 2 && (std::is_convertible_v<Cs, ColumnType> && ...))
        constexpr TMat(const Cs&... InCols) : Cols{ ColumnType(InCols)... } {}

        // From C*R scalars in column-major order.
        template<typename... Ss>
            requires (sizeof...(Ss) == C * R && C * R != C && (std::is_convertible_v<Ss, T> && ...))
        constexpr TMat(Ss... InScalars)
        {
            const T Vals[C * R] = { static_cast<T>(InScalars)... };
            for (int c = 0; c < C; ++c)
            {
                for (int r = 0; r < R; ++r)
                {
                    Cols[c][r] = Vals[c * R + r];
                }
            }
        }

        template<typename U>
        explicit constexpr TMat(const TMat<U, C, R>& M)
        {
            for (int c = 0; c < C; ++c)
            {
                Cols[c] = ColumnType(M.Cols[c]);
            }
        }

        // Resize: a smaller target keeps the upper-left; a larger one fills the rest
        // with identity). The overlapping block is copied, the remainder identity.
        template<int C2, int R2>
            requires (C2 != C || R2 != R)
        explicit constexpr TMat(const TMat<T, C2, R2>& M) : TMat(T(1))
        {
            constexpr int CC = C < C2 ? C : C2;
            constexpr int RR = R < R2 ? R : R2;
            for (int c = 0; c < CC; ++c)
            {
                for (int r = 0; r < RR; ++r)
                {
                    Cols[c][r] = M.Cols[c][r];
                }
            }
        }

        constexpr ColumnType&       operator[](int i)       { return Cols[i]; }
        constexpr const ColumnType& operator[](int i) const { return Cols[i]; }

        // Contiguous column-major scalars, for GPU upload / interop.
        T*       GetData()       { return &Cols[0][0]; }
        const T* GetData() const { return &Cols[0][0]; }

        static constexpr TMat Identity() { return TMat(T(1)); }
    };

    // ---- Core algebra (generic over TMat<T,C,R>) ----------------------------

    // M (R x C) * column vector (C) -> column vector (R): linear combination of columns.
    template<typename T, int C, int R>
    constexpr TVec<T, R> operator*(const TMat<T, C, R>& M, const TVec<T, C>& V)
    {
        TVec<T, R> Result{};
        for (int c = 0; c < C; ++c)
        {
            Result += M.Cols[c] * V[c];
        }
        return Result;
    }

    // A (R x C) * B (C x C2) -> (R x C2). Each result column is A times B's column.
    template<typename T, int C, int R, int C2>
    constexpr TMat<T, C2, R> operator*(const TMat<T, C, R>& A, const TMat<T, C2, C>& B)
    {
        TMat<T, C2, R> Result(T(0));
        for (int j = 0; j < C2; ++j)
        {
            Result.Cols[j] = A * B.Cols[j];
        }
        return Result;
    }

    template<typename T, int C, int R>
    constexpr TMat<T, C, R> operator*(const TMat<T, C, R>& M, std::type_identity_t<T> S)
    {
        TMat<T, C, R> Result(T(0));
        for (int c = 0; c < C; ++c)
        {
            Result.Cols[c] = M.Cols[c] * S;
        }
        return Result;
    }

    template<typename T, int C, int R>
    constexpr TMat<T, C, R> operator*(std::type_identity_t<T> S, const TMat<T, C, R>& M)
    {
        return M * S;
    }

    template<typename T, int C, int R>
    constexpr TMat<T, C, R> operator+(const TMat<T, C, R>& A, const TMat<T, C, R>& B)
    {
        TMat<T, C, R> Result(T(0));
        for (int c = 0; c < C; ++c)
        {
            Result.Cols[c] = A.Cols[c] + B.Cols[c];
        }
        return Result;
    }

    template<typename T, int C, int R>
    constexpr TMat<T, C, R> operator-(const TMat<T, C, R>& A, const TMat<T, C, R>& B)
    {
        TMat<T, C, R> Result(T(0));
        for (int c = 0; c < C; ++c)
        {
            Result.Cols[c] = A.Cols[c] - B.Cols[c];
        }
        return Result;
    }

    template<typename T, int C, int R>
    constexpr bool operator==(const TMat<T, C, R>& A, const TMat<T, C, R>& B)
    {
        for (int c = 0; c < C; ++c)
        {
            if (A.Cols[c] != B.Cols[c]) { return false; }
        }
        return true;
    }

    template<typename T, int C, int R>
    constexpr bool operator!=(const TMat<T, C, R>& A, const TMat<T, C, R>& B)
    {
        return !(A == B);
    }

    // ---- Concrete aliases ---------------------------------------------------

    using FMatrix2 = TMat<float, 2, 2>;
    using FMatrix3 = TMat<float, 3, 3>;
    using FMatrix4 = TMat<float, 4, 4>;
    using FMatrix  = FMatrix4;

    using FDoubleMatrix3 = TMat<double, 3, 3>;
    using FDoubleMatrix4 = TMat<double, 4, 4>;
}
