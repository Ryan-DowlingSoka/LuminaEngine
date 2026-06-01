#pragma once

#include "Containers/Array.h"
// entt (entt::type_hash) is provided via the precompiled header, like the rest of the World module.

namespace Lumina
{
    // Declared component/resource access for an ECS system, used to decide which systems may run
    // concurrently. Two systems conflict (must serialize) if their writes overlap, or one writes what
    // the other reads. IDs are entt::type_hash values for the component or SystemResource:: tag types.
    //
    // A system opts in by adding a static member `Access`, e.g.:
    //     static inline FSystemAccess Access = FSystemAccess{}.Write<SSkeletalMeshComponent>()
    //                                                          .Read<SAnimationGraphComponent>();
    // A system with NO Access member is treated as EXCLUSIVE (conflicts with everything → runs alone),
    // which is the safe default for anything doing structural changes, Lua, or unknown access.
    struct FSystemAccess
    {
        TVector<uint32> Writes;
        TVector<uint32> Reads;
        bool            bExclusive = false;

        template<typename... Ts>
        FSystemAccess& Write()
        {
            (Writes.push_back(static_cast<uint32>(entt::type_hash<Ts>::value())), ...);
            return *this;
        }

        template<typename... Ts>
        FSystemAccess& Read()
        {
            (Reads.push_back(static_cast<uint32>(entt::type_hash<Ts>::value())), ...);
            return *this;
        }

        static FSystemAccess Exclusive()
        {
            FSystemAccess A;
            A.bExclusive = true;
            return A;
        }

        // Sets are tiny (a handful of types), so a nested scan beats hashing.
        static bool Intersects(const TVector<uint32>& X, const TVector<uint32>& Y)
        {
            for (uint32 A : X)
            {
                for (uint32 B : Y)
                {
                    if (A == B)
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        static bool Conflicts(const FSystemAccess& A, const FSystemAccess& B)
        {
            if (A.bExclusive || B.bExclusive)
            {
                return true;
            }
            return Intersects(A.Writes, B.Writes)
                || Intersects(A.Writes, B.Reads)
                || Intersects(B.Writes, A.Reads);
        }
    };
}
