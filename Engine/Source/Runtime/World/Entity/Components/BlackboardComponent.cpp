#include "pch.h"
#include "BlackboardComponent.h"

namespace Lumina
{
    static FBlackboardValue MakeDefaultValue(const FBlackboardKey& Key)
    {
        FBlackboardValue Value;
        switch (Key.Type)
        {
        case EBlackboardKeyType::Float:  Value.Scalar = Key.DefaultFloat;              break;
        case EBlackboardKeyType::Int:    Value.Scalar = (float)Key.DefaultInt;         break;
        case EBlackboardKeyType::Bool:   Value.Scalar = Key.DefaultBool ? 1.0f : 0.0f; break;
        case EBlackboardKeyType::Enum:   Value.Scalar = (float)Key.DefaultInt;         break;
        case EBlackboardKeyType::Vector: Value.Vector = Key.DefaultVector;             break;
        case EBlackboardKeyType::Object: Value.Object = Key.DefaultObject;             break;
        }
        return Value;
    }

    void SBlackboardComponent::EnsureInitialized()
    {
        const CBlackboard* Schema = Blackboard.Get();

        // Schema asset swapped out from under us -> rebuild from scratch.
        if (Schema != SeededSchema)
        {
            Values.clear();
            SeededSchema = Schema;
        }

        if (Schema == nullptr)
        {
            return;
        }

        for (const FBlackboardKey& Key : Schema->Keys)
        {
            if (Key.Name.IsNone() || Values.find(Key.Name) != Values.end())
            {
                continue;
            }
            Values[Key.Name] = MakeDefaultValue(Key);
        }
    }

    void SBlackboardComponent::SetFloat(const FName& Key, float Value)
    {
        EnsureInitialized();
        Values[Key].Scalar = Value;
    }

    float SBlackboardComponent::GetFloat(const FName& Key, float Default) const
    {
        auto It = Values.find(Key);
        return It == Values.end() ? Default : It->second.Scalar;
    }

    void SBlackboardComponent::SetInt(const FName& Key, int32 Value)
    {
        EnsureInitialized();
        Values[Key].Scalar = (float)Value;
    }

    int32 SBlackboardComponent::GetInt(const FName& Key, int32 Default) const
    {
        auto It = Values.find(Key);
        return It == Values.end() ? Default : (int32)glm::round(It->second.Scalar);
    }

    void SBlackboardComponent::SetBool(const FName& Key, bool bValue)
    {
        SetFloat(Key, bValue ? 1.0f : 0.0f);
    }

    bool SBlackboardComponent::GetBool(const FName& Key, bool Default) const
    {
        return GetFloat(Key, Default ? 1.0f : 0.0f) != 0.0f;
    }

    void SBlackboardComponent::SetObjectValue(const FName& Key, CObject* Value)
    {
        EnsureInitialized();
        Values[Key].Object = Value;
    }

    CObject* SBlackboardComponent::GetObjectValue(const FName& Key) const
    {
        auto It = Values.find(Key);
        return It == Values.end() ? nullptr : It->second.Object.Get();
    }

    void SBlackboardComponent::SetVector(const FName& Key, const glm::vec3& Value)
    {
        EnsureInitialized();
        Values[Key].Vector = Value;
    }

    glm::vec3 SBlackboardComponent::GetVector(const FName& Key, const glm::vec3& Default) const
    {
        auto It = Values.find(Key);
        return It == Values.end() ? Default : It->second.Vector;
    }

    bool SBlackboardComponent::HasKey(const FName& Key) const
    {
        return Blackboard.IsValid() && Blackboard->FindKeyIndex(Key) != INDEX_NONE;
    }
}
