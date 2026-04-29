#pragma once
#include "Containers/String.h"
#include "Core/Delegates/Delegate.h"
#include "Core/Reflection/Type/LuminaTypes.h"
#include "Memory/SmartPtr.h"
#include "Tools/Actions/DeferredActions.h"


struct GCMetrics;
struct lua_State;

namespace Lumina
{
    class CStruct;
}

namespace Lumina::Lua
{
    class FRef;
    struct FScript;
    DECLARE_MULTICAST_DELEGATE(FScriptTransactionDelegate, FStringView);


    struct FLuaScriptMetadata;
    void Initialize();
    void Shutdown();

    // ------------------------------------------------------------------------
    // Symbol harvest — walks the live VM globals so the editor can offer
    // autocomplete that stays in sync with whatever Scripting.cpp registers.
    enum class ELuaSymbolKind : uint8
    {
        Table,
        Function,
        Value,
    };

    struct FLuaSymbol
    {
        FString         Name;       // Leaf name, e.g. "ReadFile"
        FString         Path;       // Dotted path, e.g. "Engine.VFS.ReadFile"
        FString         Parent;     // Empty for top-level, else parent path e.g. "Engine.VFS"
        ELuaSymbolKind  Kind = ELuaSymbolKind::Value;
    };

    
    class FScriptingContext
    {
        struct FScriptLoad
        {
            FScriptLoad(FStringView Str)
                : Path(Str)
            {}
            
            FString Path;
        };
        
        struct FScriptDelete
        {
            FScriptDelete(FStringView Str)
                : Path(Str)
            {}
            
            FString Path;
        };
        
        struct FScriptRename
        {
            FScriptRename(FStringView A, FStringView B)
                : NewName(A), OldName(B)
            {}
            
            FString NewName;
            FString OldName;
        };
        
    public:

        RUNTIME_API static FScriptingContext& Get();
        
        void Initialize();
        void SandboxGlobals();
        void Shutdown();
        
        void ProcessDeferredActions();

        RUNTIME_API int GetScriptMemoryUsageBytes() const;
        RUNTIME_API void ScriptReloaded(FStringView ScriptPath);
        RUNTIME_API void ScriptCreated(FStringView ScriptPath);
        RUNTIME_API void ScriptRenamed(FStringView NewPath, FStringView OldPath);
        RUNTIME_API void ScriptDeleted(FStringView ScriptPath);
        RUNTIME_API TSharedPtr<FScript> LoadUniqueScriptPath(FStringView Path);
        RUNTIME_API TSharedPtr<FScript> LoadUniqueScript(FStringView Code, FStringView Name = "") const;
        RUNTIME_API TVector<TSharedPtr<FScript>> GetAllRegisteredScripts();
        RUNTIME_API void RunGC();
        RUNTIME_API FRef GetGlobalsRef() const;

        // Walks LUA_GLOBALSINDEX one level deep into nested tables and
        // returns symbol info the editor uses for autocomplete. Stays in
        // sync with whatever Initialize() registers — no separate manifest.
        RUNTIME_API void HarvestGlobalSymbols(TVector<FLuaSymbol>& Out);
        
        #if LUAI_GCMETRICS
        RUNTIME_API const GCMetrics* GetGCMetrics() const;  
        #endif
        
        FScriptTransactionDelegate OnScriptLoaded;
        FScriptTransactionDelegate OnScriptDeleted;

    public:
        
        RUNTIME_API lua_State* GetVM() const { return L; }
        
    private:
        
        void ReloadScripts(FStringView Path);
    
    private:
        
        lua_State* L = nullptr;
        
        FSharedMutex SharedMutex;
        FDeferredActionRegistry DeferredActions;
        
        THashMap<FName, TVector<TWeakPtr<FScript>>> RegisteredScripts;
    };
    
}

#include "Scripting.inl"