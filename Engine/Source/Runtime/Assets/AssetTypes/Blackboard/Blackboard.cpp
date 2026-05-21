#include "pch.h"
#include "Blackboard.h"

namespace Lumina
{
    int32 CBlackboard::FindKeyIndex(const FName& Name) const
    {
        for (int32 i = 0; i < (int32)Keys.size(); ++i)
        {
            if (Keys[i].Name == Name)
            {
                return i;
            }
        }
        return INDEX_NONE;
    }

    const FBlackboardKey* CBlackboard::FindKey(const FName& Name) const
    {
        const int32 Index = FindKeyIndex(Name);
        return Index == INDEX_NONE ? nullptr : &Keys[Index];
    }
}
