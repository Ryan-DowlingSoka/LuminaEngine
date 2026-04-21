#pragma once

#include "Containers/Array.h"
#include "Containers/Function.h"
#include "Core/LuminaMacros.h"
#include "Scripting/Lua/Reference.h"
#include "entt/entt.hpp"

namespace Lumina
{
    /**
     * Opaque handle returned by FTimerManager::SetTimer. Safe to keep across frames,
     * the underlying entt::entity is generational, so a stale handle will report invalid
     * through FTimerManager::IsTimerActive even if the slot is later recycled.
     */
    struct FTimerHandle
    {
        entt::entity Handle = entt::null;

        bool IsValid() const { return Handle != entt::null; }
        void Invalidate()    { Handle = entt::null; }

        bool operator==(const FTimerHandle& Other) const { return Handle == Other.Handle; }
    };

    /**
     * Per-world timer manager. Drives one-shot and looping callbacks against the
     * world's delta time. Ticks advance only while the world is unpaused.
     *
     * Lua API (available as the "Timer" global inside every script):
     *
     *   local h = Timer:Delay(1.5, function() print("hi") end)
     *   local h = Timer:SetTimer(0.5, function() ... end, true)       -- looping
     *   Timer:SetEntityTimer(self.Entity, 2.0, function() ... end)    -- auto-cleared on entity destroy
     *   Timer:ClearTimer(h)
     *   Timer:PauseTimer(h, true)
     *   local remaining = Timer:GetTimerRemaining(h)
     */
    class RUNTIME_API FTimerManager
    {
    public:

        using FTimerCallback = TFunction<void()>;

        FTimerManager() = default;
        ~FTimerManager();
        LE_NO_COPYMOVE(FTimerManager);

        static void RegisterLuaModule(Lua::FRef& GlobalRef);

        FTimerHandle SetTimer(float Rate, FTimerCallback Callback, bool bLoop = false, float FirstDelay = -1.0f);
        FTimerHandle SetTimerForEntity(entt::entity Owner, float Rate, FTimerCallback Callback, bool bLoop = false, float FirstDelay = -1.0f);

        void ClearTimer(FTimerHandle& Handle);
        void ClearTimersForEntity(entt::entity Owner);
        void Clear();

        bool  IsTimerActive(FTimerHandle Handle) const;
        bool  IsTimerPaused(FTimerHandle Handle) const;
        float GetTimerRate(FTimerHandle Handle) const;
        float GetTimerRemaining(FTimerHandle Handle) const;
        float GetTimerElapsed(FTimerHandle Handle) const;

        void SetTimerPaused(FTimerHandle Handle, bool bPause);

        void Tick(float DeltaTime);

    private:

        struct FTimer
        {
            float               Rate            = 0.0f;
            float               Remaining       = 0.0f;
            bool                bLoop           = false;
            bool                bPaused         = false;
            bool                bPendingDestroy = false;
            entt::entity        Owner           = entt::null;
            FTimerCallback      NativeCallback;
            Lua::FRef           LuaCallback;
        };

        //~ Lua binding shims (call through the script's injected Timer global).
        entt::entity SetTimer_Lua(float Rate, Lua::FRef Callback, bool bLoop);
        entt::entity SetEntityTimer_Lua(entt::entity Owner, float Rate, Lua::FRef Callback, bool bLoop);
        entt::entity Delay_Lua(float Delay, Lua::FRef Callback);
        void         ClearTimer_Lua(entt::entity Handle);
        bool         IsTimerActive_Lua(entt::entity Handle) const;
        float        GetTimerRemaining_Lua(entt::entity Handle) const;
        void         PauseTimer_Lua(entt::entity Handle, bool bPause);

        entt::entity CreateTimer(float Rate, bool bLoop, float FirstDelay, entt::entity Owner, FTimerCallback NativeCallback, Lua::FRef LuaCallback);

        mutable entt::registry  Registry;
        bool                    bTicking = false;
    };
}
