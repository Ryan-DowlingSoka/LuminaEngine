#pragma once

#include <type_traits>
#include "Core/Assertions/Assert.h"

namespace Lumina
{
    class FBitFlags
    {
    public:

        constexpr static uint8 MaxFlags = 32;
        FORCEINLINE static uint32_t GetFlagMask(uint8 flag) { return (1u << flag); }

    public:

        inline FBitFlags() = default;
        inline explicit FBitFlags(uint32 flags) : Flags(flags) {}

        FORCEINLINE uint32 Get() const { return Flags; }
        FORCEINLINE void Set(uint32 InFlags) { Flags = InFlags; }
        inline operator uint32() const { return Flags; }

        FORCEINLINE bool HasNoFlagsSet() const { return Flags == 0; }
        FORCEINLINE bool IsAnyFlagSet() const { return Flags != 0; }

        FORCEINLINE bool IsFlagSet( uint8_t flag ) const
        {
            DEBUG_ASSERT(flag < MaxFlags);
            return (Flags & GetFlagMask(flag)) > 0;
        }

        template<typename T>
        requires(eastl::is_enum_v<T>)
        FORCEINLINE bool IsFlagSet(T enumValue) const
        {
            return IsFlagSet((uint8) enumValue);
        }

        FORCEINLINE void SetFlag(uint8 flag)
        {
            Flags |= GetFlagMask(flag);
        }

        template<typename T>
        requires(eastl::is_enum_v<T>)
        FORCEINLINE void SetFlag(T enumValue)
        {
            SetFlag( (uint8_t)enumValue );
        }

        FORCEINLINE void SetFlag(uint8 flag, bool value)
        {
            DEBUG_ASSERT(flag < MaxFlags);
            value ? SetFlag(flag) : ClearFlag(flag);
        }

        template<typename T>
        requires(eastl::is_enum_v<T>)
        FORCEINLINE void SetFlag(T enumValue, bool value)
        {
            SetFlag((uint8)enumValue, value);
        }

        FORCEINLINE void SetAllFlags()
        {
            Flags = 0xFFFFFFFF;
        }

        FORCEINLINE bool IsFlagCleared(uint8 flag) const
        {
            DEBUG_ASSERT(flag < MaxFlags);
            return (Flags & GetFlagMask(flag)) == 0;
        }

        template<typename T>
        requires(eastl::is_enum_v<T>)
        FORCEINLINE bool IsFlagCleared(T enumValue)
        {
            return IsFlagCleared((uint8)enumValue);
        }

        FORCEINLINE void ClearFlag(uint8 flag)
        {
            DEBUG_ASSERT(flag < MaxFlags);
            Flags &= ~GetFlagMask(flag);
        }

        template<typename T>
        FORCEINLINE void ClearFlag(T enumValue)
        {
            ClearFlag((uint8)enumValue);
        }

        FORCEINLINE void ClearAllFlags()
        {
            Flags = 0;
        }

        FORCEINLINE void FlipFlag(uint8 flag)
        {
            DEBUG_ASSERT(flag >= 0 && flag < MaxFlags);
            Flags ^= GetFlagMask( flag );
        }

        template<typename T>
        requires std::is_enum_v<T>
        FORCEINLINE void FlipFlag(T enumValue)
        {
            FlipFlag((uint8)enumValue);
        }


        FORCEINLINE void FlipAllFlags()
        {
            Flags = ~Flags;
        }

        FORCEINLINE FBitFlags& operator | ( uint8_t flag )
        {
            DEBUG_ASSERT(flag < MaxFlags);
            Flags |= GetFlagMask( flag );
            return *this;
        }

        FORCEINLINE FBitFlags& operator & ( uint8_t flag )
        {
            DEBUG_ASSERT(flag < MaxFlags);
            Flags &= GetFlagMask(flag);
            return *this;
        }
        
        uint32 Flags = 0;
    };
}

namespace Lumina
{
    template<typename T>
    requires(eastl::is_enum_v<T> && sizeof(T) <= sizeof(uint32))
    class TBitFlags : public FBitFlags
    {
    public:

        using FBitFlags::FBitFlags;

        explicit TBitFlags(T value) 
            : FBitFlags(static_cast<uint32>(value))
        {
            DEBUG_ASSERT((uint32) value < MaxFlags);
        }

        TBitFlags(uint32 i)
            : FBitFlags(i)
        {}

        TBitFlags(const TBitFlags<T>& flags)
            : FBitFlags( flags.Flags )
        {}

        template<typename... Args>
        requires(... && eastl::is_convertible_v<Args, T>)
        TBitFlags(Args&&... args)
        {
            ((Flags |= 1u << (uint8)std::forward<Args>(args)), ...);
        }

        TBitFlags& operator=( TBitFlags const& rhs ) = default;

        FORCEINLINE bool IsFlagSet( T flag ) const { return FBitFlags::IsFlagSet( (uint8_t) flag ); }
        FORCEINLINE bool IsFlagCleared( T flag ) const { return FBitFlags::IsFlagCleared( (uint8_t) flag ); }
        FORCEINLINE void SetFlag( T flag ) { FBitFlags::SetFlag( (uint8_t) flag ); }
        FORCEINLINE void SetFlag( T flag, bool value ) { FBitFlags::SetFlag( (uint8_t) flag, value ); }
        FORCEINLINE void FlipFlag( T flag ) { FBitFlags::FlipFlag( (uint8_t) flag ); }
        FORCEINLINE void ClearFlag( T flag ) { FBitFlags::ClearFlag( (uint8_t) flag ); }

        template<typename... Args>
        FORCEINLINE void SetMultipleFlags(Args&&... args)
        {
            ((Flags |= 1u << (uint8) eastl::forward<Args>(args)), ...);
        }

        template<typename... Args>
        FORCEINLINE bool AreAnyFlagsSet(Args&&... args) const
        {
            uint32 mask = 0;
            ((mask |= 1u << (uint8) eastl::forward<Args>(args)), ...);
            return (Flags & mask) != 0;
        }

        FORCEINLINE TBitFlags& operator| ( T flag )
        {
            DEBUG_ASSERT((uint8) flag < MaxFlags);
            Flags |= GetFlagMask(flag);
            return *this;
        }

        FORCEINLINE TBitFlags& operator& ( T flag )
        {
            DEBUG_ASSERT((uint8) flag < MaxFlags);
            Flags &= GetFlagMask(flag);
            return *this;
        }
        
    };
}
