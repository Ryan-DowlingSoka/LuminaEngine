#pragma once

#include "StringHash.h"
#include "EASTL/hash_map.h"
#include "Reflector/Types/ReflectedType.h"

namespace Lumina::Reflection
{
    class FReflectionDatabase
    {
    public:

        FReflectionDatabase() = default;
        ~FReflectionDatabase() = default;

        void AddReflectedType(FReflectedType* Type);

        bool IsTypeRegistered(const FStringHash& Str) const;

        bool IsCoreType(const FStringHash& Hash) const;
        
        template<typename T>
        requires(eastl::is_base_of_v<FReflectedType, T>)
        T* GetOrCreateReflectedType(const FStringHash& TypeName);

        template<typename T>
        requires(eastl::is_base_of_v<FReflectedType, T>)
        T* GetReflectedTypeChecked(const FStringHash& TypeName) const;

        template<typename T>
        T* GetReflectedType(const FStringHash& TypeName) const;

        
        eastl::hash_map<FReflectedHeader*, eastl::vector<eastl::unique_ptr<FReflectedType>>>    ReflectedTypes;
        eastl::hash_map<FStringHash, FReflectedType*>                                           TypeHashMap;
        
    };


    template <typename T>
    requires(eastl::is_base_of_v<FReflectedType, T>)
    T* FReflectionDatabase::GetOrCreateReflectedType(const FStringHash& TypeName)
    {
        T* ReturnValue;
        if (IsTypeRegistered(TypeName))
        {
            ReturnValue = static_cast<T*>(TypeHashMap.at(TypeName));
        }
        else
        {
            ReturnValue = new T{};
        }
        ReturnValue->QualifiedName = TypeName.c_str();

        return ReturnValue;
    }

    template <typename T> requires (eastl::is_base_of_v<FReflectedType, T>)
    T* FReflectionDatabase::GetReflectedTypeChecked(const FStringHash& TypeName) const
    {
        if (!IsTypeRegistered(TypeName))
        {
            std::abort();
        }
        
        return static_cast<T*>(TypeHashMap.at(TypeName));
    }

    template <typename T>
    T* FReflectionDatabase::GetReflectedType(const FStringHash& TypeName) const
    {
        if (TypeHashMap.find(TypeName) == TypeHashMap.end())
        {
            return nullptr;
        }

        return static_cast<T*>(TypeHashMap.at(TypeName));
    }
}
