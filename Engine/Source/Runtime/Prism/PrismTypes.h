#pragma once

#include <glm/glm.hpp>
#include "Core/LuminaCommonTypes.h"

namespace Lumina::Prism
{
    struct FPrismRect
    {
        glm::vec2 Min{0.0f};
        glm::vec2 Max{0.0f};

        FPrismRect() = default;
        FPrismRect(const glm::vec2& InMin, const glm::vec2& InMax) : Min(InMin), Max(InMax) {}
        static FPrismRect FromSize(const glm::vec2& Origin, const glm::vec2& Size) { return FPrismRect(Origin, Origin + Size); }

        glm::vec2 GetSize()   const { return Max - Min; }
        glm::vec2 GetCenter() const { return (Min + Max) * 0.5f; }
        float     GetWidth()  const { return Max.x - Min.x; }
        float     GetHeight() const { return Max.y - Min.y; }

        bool Contains(const glm::vec2& P) const
        {
            return P.x >= Min.x && P.x <= Max.x && P.y >= Min.y && P.y <= Max.y;
        }

        FPrismRect Inflate(float Amount) const { return FPrismRect(Min - glm::vec2(Amount), Max + glm::vec2(Amount)); }
        FPrismRect Intersect(const FPrismRect& Other) const
        {
            FPrismRect R;
            R.Min = glm::max(Min, Other.Min);
            R.Max = glm::min(Max, Other.Max);
            if (R.Max.x < R.Min.x)
            {
                R.Max.x = R.Min.x;
            }
            if (R.Max.y < R.Min.y)
            {
                R.Max.y = R.Min.y;
            }
            return R;
        }
    };

    struct FPrismMargin
    {
        float Left   = 0.0f;
        float Top    = 0.0f;
        float Right  = 0.0f;
        float Bottom = 0.0f;

        FPrismMargin() = default;
        explicit FPrismMargin(float Uniform) : Left(Uniform), Top(Uniform), Right(Uniform), Bottom(Uniform) {}
        FPrismMargin(float H, float V) : Left(H), Top(V), Right(H), Bottom(V) {}
        FPrismMargin(float L, float T, float R, float B) : Left(L), Top(T), Right(R), Bottom(B) {}

        glm::vec2 GetTotal() const { return { Left + Right, Top + Bottom }; }
        glm::vec2 GetTopLeft() const { return { Left, Top }; }
    };

    struct FPrismColor
    {
        float R = 1.0f;
        float G = 1.0f;
        float B = 1.0f;
        float A = 1.0f;

        FPrismColor() = default;
        FPrismColor(float InR, float InG, float InB, float InA = 1.0f) : R(InR), G(InG), B(InB), A(InA) {}
        explicit FPrismColor(const glm::vec4& V) : R(V.x), G(V.y), B(V.z), A(V.w) {}

        glm::vec4 ToVec4() const { return { R, G, B, A }; }

        static FPrismColor White()  { return {1, 1, 1, 1}; }
        static FPrismColor Black()  { return {0, 0, 0, 1}; }
        static FPrismColor Red()    { return {1, 0, 0, 1}; }
        static FPrismColor Green()  { return {0, 1, 0, 1}; }
        static FPrismColor Transparent() { return {0, 0, 0, 0}; }
    };

    enum class EPrismVisibility : uint8
    {
        // Visible, hit-testable.
        Visible,
        // Drawn but transparent to input.
        HitTestInvisible,
        // Not drawn, not hit-tested, still participates in layout.
        Hidden,
        // Not drawn, not hit-tested, does not participate in layout.
        Collapsed,
    };

    enum class EPrismHAlign : uint8  { Left, Center, Right, Fill };
    enum class EPrismVAlign : uint8  { Top, Center, Bottom, Fill };
    enum class EPrismOrientation : uint8 { Horizontal, Vertical };

    enum class EPrismCursor : uint8
    {
        Default,
        Arrow,
        IBeam,
        Hand,
        SizeAll,
        SizeNS,
        SizeEW,
        NotAllowed,
    };

    // Resolved layout rectangle for a widget within its parent window.
    // All coordinates are in window pixels; Prism is currently DPI-agnostic.
    struct FPrismGeometry
    {
        glm::vec2 AbsolutePosition{0.0f};
        glm::vec2 LocalSize{0.0f};
        float     Scale = 1.0f;

        FPrismGeometry() = default;
        FPrismGeometry(const glm::vec2& InPos, const glm::vec2& InSize, float InScale = 1.0f)
            : AbsolutePosition(InPos), LocalSize(InSize), Scale(InScale) {}

        FPrismRect GetRect() const { return FPrismRect::FromSize(AbsolutePosition, LocalSize * Scale); }

        FPrismGeometry MakeChild(const glm::vec2& LocalOffset, const glm::vec2& ChildSize) const
        {
            return FPrismGeometry(AbsolutePosition + LocalOffset * Scale, ChildSize, Scale);
        }

        bool ContainsAbsolute(const glm::vec2& P) const { return GetRect().Contains(P); }
    };
}
