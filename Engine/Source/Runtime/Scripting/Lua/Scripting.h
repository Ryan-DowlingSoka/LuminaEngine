#pragma once
#include "Containers/String.h"
#include "Core/Delegates/Delegate.h"
#include "Core/Reflection/Type/LuminaTypes.h"
#include "Memory/SmartPtr.h"
#include "ScriptExports.h"
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

    // Per-path cache; subsequent loads skip file read + luau_compile + schema build.
    struct FScriptCacheEntry
    {
        TVector<uint8>                  Bytecode;
        FScriptExportSchema             ExportsSchema;
        TVector<FScriptPropertyEntry>   ExportDefaults;
    };


    struct FLuaScriptMetadata;
    void Initialize();
    void Shutdown();

    // Symbol harvest from live VM globals for editor autocomplete.
    enum class ELuaSymbolKind : uint8
    {
        Table,
        Function,
        Value,
    };

    struct FLuaSymbol
    {
        FString         Name;
        FString         Path;          // Dotted, e.g. "Engine.VFS.ReadFile".
        FString         Parent;
        ELuaSymbolKind  Kind = ELuaSymbolKind::Value;

        FString         TypeName;      // Precise lua_typename; Kind collapses primitives into Value.
        FString         ValuePreview;  // Literal text for primitives only.

        // Lua functions: real arity. C functions: ParamCount=0, bIsVararg=true (Luau can't introspect).
        uint8           ParamCount = 0;
        bool            bIsVararg = false;
        bool            bIsCFunction = false;

        // Curated overlay for known stdlib symbols.
        TVector<FString> ParamNames;
        FString          Description;
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

        // Drops cached bytecode/schema; next load re-reads + recompiles.
        RUNTIME_API void InvalidateScriptCache(FStringView Path);

        // Walks LUA_GLOBALSINDEX one level deep for editor autocomplete.
        RUNTIME_API void HarvestGlobalSymbols(TVector<FLuaSymbol>& Out);

        // Re-runs Stdlib *.luau against the live VM; `Foo = Foo or {}` keeps existing instance metatables hot.
        // Exposed as `Engine.ReloadStdlib()`.
        RUNTIME_API void ReloadStdlib();
        
        #if LUAI_GCMETRICS
        RUNTIME_API const GCMetrics* GetGCMetrics() const;  
        #endif
        
        FScriptTransactionDelegate OnScriptLoaded;
        FScriptTransactionDelegate OnScriptDeleted;

    public:
        
        RUNTIME_API lua_State* GetVM() const { return L; }
        
    private:

        void LoadStdlibFiles();

        void ReloadScripts(FStringView Path);

        // Out* params (when non-null) are populated by walking the loaded module; seeds the cache on cold load.
        TSharedPtr<FScript> InstantiateFromBytecode(
            const TVector<uint8>& Bytecode,
            FStringView Name,
            FScriptExportSchema* OutSchema,
            TVector<FScriptPropertyEntry>* OutDefaults) const;

    private:
        
        lua_State* L = nullptr;
        
        FSharedMutex SharedMutex;
        FDeferredActionRegistry DeferredActions;

        THashMap<FName, TVector<TWeakPtr<FScript>>> RegisteredScripts;

        THashMap<FName, FScriptCacheEntry> ScriptCache;
    };
    
}

#include "Scripting.inl"