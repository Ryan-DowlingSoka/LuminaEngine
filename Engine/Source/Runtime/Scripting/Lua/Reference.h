#pragma once

#include "Class.h"
#include "Error.h"
#include "Invoker.h"
#include "lua.h"
#include "Stack.h"
#include "Log/Log.h"


namespace Lumina::Lua
{
    enum class EType : uint8;

    /**
     * Result of resuming a Lua coroutine. `Yielded` means the coroutine is suspended
     * and is being kept alive by whoever requested the yield (e.g. FTimerManager:Wait
     * pinning the thread in the registry until the timer fires).
     */
    enum class ECoroutineStatus : uint8
    {
        Ok,
        Yielded,
        Error,
    };

    /**
     * A reference to a lua object.
     *
     * This will pin down a reference to a lua-allocated object and prevent it from being garbage collected.
     * Once this class is cleaned up, it will release the reference, and may allow GC again.
     */
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

        /**
         * Invoke this function on a freshly spawned sub-coroutine. The sub-thread is
         * pinned in the Lua registry while running and released on completion or error.
         * If the function yields (e.g. via FTimerManager:Wait), responsibility for the
         * thread's lifetime transfers to whoever requested the yield.
         *
         * Use this for entry points where the script is allowed to call yield-aware APIs
         * such as `TimerManager:Wait`. Plain `Invoke` (lua_pcall) cannot yield.
         */
        template<typename... TArgs>
        ECoroutineStatus InvokeAsCoroutine(TArgs&& ... Args) const;

        template<typename T>
        NODISCARD TClass<T> NewClass(FStringView Name);
        
        
        void Reset();
        
        NODISCARD FString ToString() const;
        NODISCARD EType GetType() const;
        NODISCARD void Push() const;

        /**
         * Push the referenced value onto a specific target state's stack. Lua refs are
         * global to the lua_State family (main + all coroutines/threads), so pushing
         * onto any thread in the family is valid. Required when the target state isn't
         * the FRef's owning state — e.g. pushing a value as an argument to a function
         * being resumed on a freshly spawned sub-coroutine.
         */
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
        
        lua_State*      State   = nullptr;
        int             Ref     = LUA_NOREF;
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
            lua_pushlightuserdatatagged(State, Instance, TClassTraits<ClassT>::Tag());
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
        return TStack<T>::Get(State, -1);
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
        return Value;    }

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
    ECoroutineStatus FRef::InvokeAsCoroutine(TArgs&&... Args) const
    {
        if (State == nullptr || Ref == LUA_NOREF)
        {
            return ECoroutineStatus::Error;
        }

        // Spawn sub-thread off the parent state. Pin in registry so the GC doesn't reclaim
        // it before lua_resume is done with it.
        lua_State* SubThread = lua_newthread(State);
        const int ThreadRef = lua_ref(State, -1);
        lua_pop(State, 1);

        // Propagate per-thread data (e.g. owning entity) so yield-aware APIs called inside
        // the coroutine can find their script context.
        if (void* ParentData = lua_getthreaddata(State))
        {
            lua_setthreaddata(SubThread, ParentData);
        }

        // Lua refs are global to the lua_State family — getref onto the sub-thread is fine.
        lua_getref(SubThread, Ref);
        DEBUG_ASSERT(lua_type(SubThread, -1) == LUA_TFUNCTION);

        (TStack<eastl::decay_t<TArgs>>::Push(SubThread, eastl::forward<TArgs>(Args)), ...);

        const int Status = lua_resume(SubThread, State, sizeof...(Args));

        if (Status == LUA_YIELD || Status == LUA_BREAK)
        {
            // Whoever yielded (e.g. FTimerManager:Wait) or broke (FLuaDebugger
            // hitting a breakpoint via lua_break) holds its own ref to keep the
            // thread alive until resumed; release ours.
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

            // Push onto the target state, not the FRef's owning state. Critical when
            // pushing FRef args into a sub-coroutine's stack via InvokeAsCoroutine,
            // since the FRef typically lives on the script's main thread.
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
