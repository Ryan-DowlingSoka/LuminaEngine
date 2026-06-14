#pragma once

#include "Class.h"
#include "Error.h"
#include "Invoker.h"
#include "lua.h"
#include "LuaTypes.h"
#include "Stack.h"
#include "Log/Log.h"


namespace Lumina::Lua
{

    // Yielded means the coroutine is suspended and pinned by whoever requested the yield.
    enum class ECoroutineStatus : uint8
    {
        Ok,
        Yielded,
        Error,
    };

    // Pins a lua-allocated object via lua_ref so it survives GC until destruction.
    class RUNTIME_API FRef
    {
    public:
        
        class FIterator
        {
        public:
            
            FIterator(lua_State* L, bool bEnd = false)
                : State(L)
                , bAtEnd(bEnd)
            {
                if (!bAtEnd)
                {
                    TableIdx = lua_gettop(L);
                    lua_pushnil(L);
                    Advance();
                }
            }

            ~FIterator()
            {
                if (!bAtEnd)
                {
                    lua_pop(State, 3);
                }
            }

            LE_NO_COPYMOVE(FIterator);

            TPair<FRef, FRef> operator*() const
            {
                lua_pushvalue(State, -2);
                FRef Key(State, -1);
                lua_pushvalue(State, -1);
                FRef Value(State, -1);
                return eastl::make_pair(eastl::move(Key), eastl::move(Value));
            }

            FIterator& operator++()
            {
                lua_pop(State, 1);
                Advance();
                return *this;
            }


            bool operator==(const FIterator& Other) const { return bAtEnd == Other.bAtEnd; }
            bool operator!=(const FIterator& Other) const { return !(*this == Other); }


        private:

            void Advance()
            {
                if (!lua_next(State, TableIdx))
                {
                    bAtEnd = true;
                    lua_pop(State, 1);
                }
            }

            lua_State* State = nullptr;
            int TableIdx = 0;
            bool bAtEnd = false;
        };
        
        FRef() = default;
        FRef(FNil);
        FRef(lua_State* L, int Index);
        ~FRef();
        
        FRef(const FRef& Other);
        FRef& operator=(const FRef& Other);
        
        FRef(FRef&& Other) noexcept;
        FRef& operator=(FRef&& Other) noexcept;
        
        template<typename T>
        requires(!eastl::is_function_v<T> && !eastl::is_member_function_pointer_v<T>)
        void Set(FStringView Key, T Value);
        
        template<typename T>
        requires(!eastl::is_function_v<T> && !eastl::is_member_function_pointer_v<T>)
        void RawSet(FStringView Key, T Value);
        
        template<auto TFunc, typename TClass = void>
        void SetFunction(FStringView Key, TClass* Instance = nullptr);
        
        template<auto TFunc, typename TClass = void>
        void SetFunction(EMetaMethod Meta, TClass* Instance = nullptr);

        // Assign a raw lua_CFunction to a table field. For variadic / custom-stack builtins that
        // don't fit the typed Invoker (e.g. loggers); mirrors SetFunction's table handling.
        void SetRawFunction(FStringView Key, lua_CFunction Fn);

        template<typename T>
        NODISCARD bool Is() const;
        
        template<typename T>
        NODISCARD TOptional<T> As() const;
        
        template<typename T>
        NODISCARD TOptional<T> Get(FStringView Key);
        
        template<typename T>
        NODISCARD TOptional<T> GetI(int Index); 
        
        template<typename... TArgs>
        NODISCARD FRef Invoke(TArgs&& ... Args);

        // Fire-and-forget pcall (0 returns, no result FRef) for hot-path hooks that discard the
        // result; skips the per-call lua_ref the Invoke return path would pin.
        template<typename... TArgs>
        bool Call(TArgs&& ... Args) const;

        // Invoke on a fresh sub-coroutine; required for yield-aware APIs since lua_pcall cannot yield.
        // On yield, ownership of the thread transfers to whoever requested the yield.
        template<typename... TArgs>
        ECoroutineStatus InvokeAsCoroutine(TArgs&& ... Args) const;
        
        template<typename... TArgs>
        ECoroutineStatus InvokeAsCoroutineOn(lua_State* SubThread, TArgs&& ... Args) const;

        template<typename T>
        NODISCARD TClass<T> NewClass(FStringView Name);
        
        
        void Reset();
        
        NODISCARD FString ToString() const;
        NODISCARD EType GetType() const;
        NODISCARD void Push() const;

        // Push onto a target thread in the same lua_State family (main + coroutines).
        NODISCARD void Push(lua_State* TargetState) const;

        NODISCARD FRef NewTable(FStringView Key) const;
        NODISCARD bool IsValid() const;
        NODISCARD FRef GetField(FStringView Key) const;
        NODISCARD FRef GetIndex(int Index) const;

        NODISCARD FRef RawGetField(FStringView Key) const;
        NODISCARD bool IsInvokable() const;
        NODISCARD bool IsTable() const;
        NODISCARD bool IsUserdata(int Tag) const;
        NODISCARD lua_State* GetState() const { return State; }
    
        NODISCARD FIterator begin() const
        {
            Push();
            return {State, false};
        }
        
        NODISCARD FIterator end() const
        {
            return {State, true};
        }
        
    public:
        
        template<typename... TArgs>
        FRef operator()(TArgs&&... Args)
        {
            return Invoke(eastl::forward<TArgs>(Args)...);
        }
        
        FRef operator [](FStringView Key) const
        {
            return GetField(Key);
        }
        
        FRef operator [](int Index) const
        {
            return GetIndex(Index);
        }
        
        explicit operator bool() const { return IsValid(); }

        bool operator==(const FRef& Other) const
        {
            if (State != Other.State)
            {
                return false;
            }
            
            Push();
            Other.Push();
            bool Result = lua_rawequal(State, -1, -2);
            lua_pop(State, 2);
            return Result;
        }    
        
    private:

        lua_State*      State       = nullptr;
        int             Ref         = LUA_NOREF;
        // The ref's value is fixed for its lifetime, so IsInvokable/IsTable/GetType can use this
        // cache instead of a lua_getref + lua_pop round-trip.
        EType           CachedType  = EType::Nil;
    };

    template <typename T>
    requires(!eastl::is_function_v<T> && !eastl::is_member_function_pointer_v<T>)
    void FRef::Set(FStringView Key, T Value)
    {
        Push();
        TStack<T>::Push(State, eastl::forward<T>(Value));
        lua_setfield(State, -2, Key.data());
        lua_pop(State, 1);
    }

    template <typename T> requires (!eastl::is_function_v<T> && !eastl::is_member_function_pointer_v<T>)
    void FRef::RawSet(FStringView Key, T Value)
    {
        Push();
        TStack<T>::Push(State, eastl::forward<T>(Value));
        lua_rawsetfield(State, -2, Key.data());
        lua_pop(State, 1);
    }

    template <auto TFunc, typename TClass>
    void FRef::SetFunction(FStringView Key, TClass* Instance)
    {
        using ClassT = eastl::decay_t<TClass>;
        
        Push();
        
        if constexpr (eastl::is_void_v<TClass>)
        {
            lua_pushcfunction(State, [](lua_State* L)
            {
                return LightInvoker<TFunc>(L);
            }, Key.data());
        }
        else
        {
            lua_pushlightuserdatatagged(State, Instance, LightTag_Self);
            lua_pushcclosure(State, [](lua_State* L)
            {
                return InvokerWithInstance<TFunc>(L);
            }, Key.data(), 1);
        }
        
        lua_rawsetfield(State, -2, Key.data());
        lua_pop(State, 1);
    }

    template <auto TFunc, typename TClass>
    void FRef::SetFunction(EMetaMethod Meta, TClass* Instance)
    {
        SetFunction<TFunc, TClass>(MetaMethodName(Meta), Instance);
    }

    template <typename T>
    bool FRef::Is() const
    {
        Push();
        
        bool Result = TStack<T>::Check(State, -1);
        lua_pop(State, 1);
        return Result;
    }

    template <typename T>
    TOptional<T> FRef::As() const
    {
        Push();
        TOptional<T> Value;
        if (TStack<T>::Check(State, -1))
        {
            Value = TStack<T>::Get(State, -1);
        }
        lua_pop(State, 1);
        return Value;
    }

    template <typename T>
    TOptional<T> FRef::Get(FStringView Key)
    {

        Push();
        lua_getfield(State, -1, Key.data());
        lua_remove(State, -2);

        TOptional<T> Value;
        if (TStack<T>::Check(State, -1))
        {
            Value = TStack<T>::Get(State, -1);
        }
        lua_pop(State, 1);
        return Value;
    }

    template <typename T>
    TOptional<T> FRef::GetI(int Index)
    {
        Push();
        lua_rawgeti(State, -1, Index);
        lua_remove(State, -2);

        TOptional<T> Value;
        if (TStack<T>::Check(State, -1))
        {
            Value = TStack<T>::Get(State, -1);
        }
        lua_pop(State, 1);
        return Value;
    }

    template <typename ... TArgs>
    FRef FRef::Invoke(TArgs&&... Args)
    {
        Push();
        DEBUG_ASSERT(lua_type(State, -1) == LUA_TFUNCTION);
        
        (TStack<eastl::decay_t<TArgs>>::Push(State, eastl::forward<TArgs>(Args)), ...);
        
        int Status = lua_pcall(State, sizeof...(Args), 1, 0);
        
        if (Status != LUA_OK)
        {
            const char* ErrMsg = lua_tostring(State, -1);
            LOG_ERROR("[Lua] - Invoke Failed {}", ErrMsg);
            lua_pop(State, 1);
            return {};
        }
        
        FRef Result(State, -1);
        return Result;
    }

    template <typename ... TArgs>
    bool FRef::Call(TArgs&&... Args) const
    {
        if (State == nullptr || Ref == LUA_NOREF)
        {
            return false;
        }

        Push();
        DEBUG_ASSERT(lua_type(State, -1) == LUA_TFUNCTION);

        (TStack<eastl::decay_t<TArgs>>::Push(State, eastl::forward<TArgs>(Args)), ...);

        const int Status = lua_pcall(State, sizeof...(Args), 0, 0);
        if (Status != LUA_OK)
        {
            const char* ErrMsg = lua_tostring(State, -1);
            LOG_ERROR("[Lua] - Call failed: {}", ErrMsg ? ErrMsg : "<unknown>");
            lua_pop(State, 1);
            return false;
        }
        return true;
    }

    template <typename ... TArgs>
    ECoroutineStatus FRef::InvokeAsCoroutine(TArgs&&... Args) const
    {
        if (State == nullptr || Ref == LUA_NOREF)
        {
            return ECoroutineStatus::Error;
        }

        // Pin sub-thread in registry so GC won't reclaim it during lua_resume.
        lua_State* SubThread = lua_newthread(State);
        const int ThreadRef = lua_ref(State, -1);
        lua_pop(State, 1);

        // Propagate per-thread data so yield-aware APIs find their script context.
        if (void* ParentData = lua_getthreaddata(State))
        {
            lua_setthreaddata(SubThread, ParentData);
        }

        lua_getref(SubThread, Ref);
        DEBUG_ASSERT(lua_type(SubThread, -1) == LUA_TFUNCTION);

        (TStack<eastl::decay_t<TArgs>>::Push(SubThread, eastl::forward<TArgs>(Args)), ...);

        const int Status = lua_resume(SubThread, State, sizeof...(Args));

        if (Status == LUA_YIELD || Status == LUA_BREAK)
        {
            // Yielder/debugger holds its own ref to keep the thread alive; release ours.
            lua_unref(State, ThreadRef);
            return ECoroutineStatus::Yielded;
        }

        if (Status != LUA_OK)
        {
            const char* ErrMsg = lua_tostring(SubThread, -1);
            LOG_ERROR("[Lua] - Coroutine failed: {}", ErrMsg ? ErrMsg : "<unknown>");
            lua_unref(State, ThreadRef);
            return ECoroutineStatus::Error;
        }

        lua_unref(State, ThreadRef);
        return ECoroutineStatus::Ok;
    }

    template <typename ... TArgs>
    ECoroutineStatus FRef::InvokeAsCoroutineOn(lua_State* SubThread, TArgs&&... Args) const
    {
        if (State == nullptr || Ref == LUA_NOREF || SubThread == nullptr)
        {
            return ECoroutineStatus::Error;
        }

        lua_getref(SubThread, Ref);
        DEBUG_ASSERT(lua_type(SubThread, -1) == LUA_TFUNCTION);

        (TStack<eastl::decay_t<TArgs>>::Push(SubThread, eastl::forward<TArgs>(Args)), ...);

        const int Status = lua_resume(SubThread, State, sizeof...(Args));

        if (Status == LUA_YIELD || Status == LUA_BREAK)
        {
            return ECoroutineStatus::Yielded;
        }
        if (Status != LUA_OK)
        {
            const char* ErrMsg = lua_tostring(SubThread, -1);
            LOG_ERROR("[Lua] - Coroutine failed: {}", ErrMsg ? ErrMsg : "<unknown>");
            return ECoroutineStatus::Error;
        }
        return ECoroutineStatus::Ok;
    }

    template <typename T>
    TClass<T> FRef::NewClass(FStringView Name)
    {
        Push();
        TClass<T> Class(State, Name);
        return Class;
    }
    
    template<>
    struct TStack<FRef>
    {
        static void Push(lua_State* State, const FRef& Value)
        {
            if (!Value.IsValid())
            {
                lua_pushnil(State);
                return;
            }

            // Push onto target state, not owning state, required for sub-coroutine arg passing.
            Value.Push(State);
        }

        static bool Check(lua_State* State, int Index)
        {
            return !lua_isnone(State, Index);
        }

        static FRef Get(lua_State* State, int Index)
        {
            lua_pushvalue(State, Index);
            return FRef(State, -1);
        }
    };
    
}
