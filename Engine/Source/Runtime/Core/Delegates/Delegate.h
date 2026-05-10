#pragma once

#include "Containers/Array.h"
#include "Containers/Function.h"
#include "Core/Assertions/Assert.h"

namespace Lumina
{
    // Handle for tracking delegates
    struct FDelegateHandle
    {
        uint64 ID = 0;
        
        bool IsValid() const { return ID != 0; }
        void Reset() { ID = 0; }
        
        bool operator==(const FDelegateHandle& Other) const { return ID == Other.ID; }
        bool operator!=(const FDelegateHandle& Other) const { return ID != Other.ID; }
    };

    template<typename R, typename ... TArgs>
    class TBaseDelegate
    {
    public:
        using FFunc = TFunction<R(TArgs...)>;

        TBaseDelegate() = default;
        
        template<typename TFunc>
        requires(eastl::is_invocable_r_v<R, TFunc, TArgs...>)
        static TBaseDelegate CreateStatic(TFunc&& Func)
        {
            TBaseDelegate Delegate;
            Delegate.Func = eastl::forward<TFunc>(Func);
            return Delegate;
        }

        template<typename TObject, typename TMemFunc>
        static TBaseDelegate CreateMember(TObject* Object, TMemFunc Method)
        {
            TBaseDelegate Delegate;
            Delegate.Func = [Object, Method](TArgs... args) -> R
            {
                return (Object->*Method)(eastl::forward<TArgs>(args)...);
            };
            return Delegate;
        }

        template<typename TLambda>
        requires(eastl::is_invocable_r_v<R, TLambda, TArgs...>)
        static TBaseDelegate CreateLambda(TLambda&& Lambda)
        {
            TBaseDelegate Delegate;
            Delegate.Func = eastl::forward<TLambda>(Lambda);
            return Delegate;
        }

        bool IsBound() const { return static_cast<bool>(Func); }

        template<typename... TCallArgs>
        R Execute(TCallArgs&&... Args) const
        {
            ASSERT(Func);

            if constexpr (eastl::is_void_v<R>)
            {
                Func(eastl::forward<TCallArgs>(Args)...);
                return;
            }
            else
            {
                return Func(eastl::forward<TCallArgs>(Args)...);
            }
        }

        void Unbind() { Func = nullptr; }

    private:
        FFunc Func;
    };
    
    template<typename R, typename... Args>
    class TMulticastDelegate
    {
    public:
        using FBase = TBaseDelegate<R, Args...>;
        
        TMulticastDelegate() noexcept = default;

        template<typename TFunc>
        NODISCARD FDelegateHandle AddStatic(TFunc&& Func)
        {
            FDelegateHandle Handle = GenerateHandle();
            InvocationList.push_back({Handle, FBase::CreateStatic(eastl::forward<TFunc>(Func))});
            return Handle;
        }

        template<typename TObject, typename TMemFunc>
        NODISCARD FDelegateHandle AddMember(TObject* Object, TMemFunc Method)
        {
            FDelegateHandle Handle = GenerateHandle();
            InvocationList.push_back({Handle, FBase::CreateMember(Object, Method)});
            return Handle;
        }

        template<typename TLambda>
        NODISCARD FDelegateHandle AddLambda(TLambda&& Lambda)
        {
            FDelegateHandle Handle = GenerateHandle();
            InvocationList.push_back({Handle, FBase::CreateLambda(eastl::forward<TLambda>(Lambda))});
            return Handle;
        }

        bool Remove(FDelegateHandle Handle)
        {
            if (!Handle.IsValid())
            {
                return false;
            }

            for (auto it = InvocationList.begin(); it != InvocationList.end(); ++it)
            {
                if (it->Handle == Handle)
                {
                    InvocationList.erase(it);
                    return true;
                }
            }
            return false;
        }

        void RemoveAll()
        {
            Clear();
        }
        
        template<typename... CallArgs>
        void Broadcast(CallArgs&&... args)
        {
            if constexpr (eastl::is_void_v<R>)
            {
                for (auto& Entry : InvocationList)
                {
                    if (Entry.Delegate.IsBound())
                    {
                        Entry.Delegate.Execute(eastl::forward<CallArgs>(args)...);
                    }
                }
            }
            else
            {
                for (auto& Entry : InvocationList)
                {
                    if (Entry.Delegate.IsBound())
                    {
                        Entry.Delegate.Execute(eastl::forward<CallArgs>(args)...);
                    }
                }
            }
        }

        template<typename... CallArgs>
        void BroadcastAndClear(CallArgs&&... args)
        {
            Broadcast(eastl::forward<CallArgs>(args)...);
            Clear();
        }

        void Clear()
        {
            InvocationList.clear();
            InvocationList.shrink_to_fit();
        }

        size_t GetCount() const { return InvocationList.size(); }

    private:
        struct FDelegateEntry
        {
            FDelegateHandle Handle;
            FBase Delegate;
        };

        static FDelegateHandle GenerateHandle()
        {
            static uint64 NextID = 1;
            FDelegateHandle Handle;
            Handle.ID = NextID++;
            return Handle;
        }
        
        TVector<FDelegateEntry> InvocationList;
    };
}

#define DECLARE_MULTICAST_DELEGATE(DelegateName, ...) \
struct DelegateName \
: public Lumina::TMulticastDelegate<void __VA_OPT__(,) __VA_ARGS__> {}

#define DECLARE_MULTICAST_DELEGATE_R(DelegateName, ...) \
struct DelegateName : public Lumina::TMulticastDelegate<__VA_ARGS__> {}