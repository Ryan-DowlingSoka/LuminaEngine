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

    /**
     * Per-source-file cache populated on first load. Subsequent loads of the same
     * script path skip the file read, luau_compile, and schema build — all
     * instances pay only luau_load + pcall to materialize a fresh module table.
     */
    struct FScriptCacheEntry
    {
        TVector<uint8>                  Bytecode;
        FScriptExportSchema             ExportsSchema;
        TVector<FScriptPropertyEntry>   ExportDefaults;
    };


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
        FString         Name;          // Leaf name, e.g. "ReadFile"
        FString         Path;          // Dotted path, e.g. "Engine.VFS.ReadFile"
        FString         Parent;        // Empty for top-level, else parent path e.g. "Engine.VFS"
        ELuaSymbolKind  Kind = ELuaSymbolKind::Value;

        // Concrete Lua type name straight from lua_typename — "function",
        // "table", "string", "number", "boolean", "vector", etc. Distinct
        // from Kind because Kind collapses every primitive into Value;
        // TypeName lets the editor show the precise type.
        FString         TypeName;

        // Preview of the runtime value for primitives. Empty for tables /
        // functions / userdata, filled with the literal text for strings,
        // numbers, booleans. Lets the autocomplete popup show "MaxHealth = 100"
        // instead of just "MaxHealth".
        FString         ValuePreview;

        // Function signature info, harvested via lua_getinfo "a" flag:
        //   - Lua functions: real declared parameter count + vararg flag.
        //   - C functions:   ParamCount = 0, bIsVararg = true (Luau can't
        //     introspect their arity, so we render them as "function(...)").
        // The editor formats these into "function(arg1, arg2, ...)" for the
        // suggestion popup detail column.
        uint8           ParamCount = 0;
        bool            bIsVararg = false;
        bool            bIsCFunction = false;

        // Curated documentation overlay. Filled in for known standard-library
        // symbols (math.sin, string.format, vector.create, ...) by the
        // harvester after the live VM walk. ParamNames lets the editor
        // render real parameter names instead of the generic "arg1, arg2";
        // Description feeds the multi-line block at the bottom of the hover
        // tooltip.
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

        // Drop the cached bytecode/schema for a path. Called when the source
        // on disk changes so the next load re-reads + re-compiles.
        RUNTIME_API void InvalidateScriptCache(FStringView Path);

        // Walks LUA_GLOBALSINDEX one level deep into nested tables and
        // returns symbol info the editor uses for autocomplete. Stays in
        // sync with whatever Initialize() registers — no separate manifest.
        RUNTIME_API void HarvestGlobalSymbols(TVector<FLuaSymbol>& Out);

        // Re-runs every Lua stdlib file (Engine/Resources/Content/Scripts/Stdlib/*.luau)
        // against the live VM. Stdlib files use `Foo = Foo or {}` for their
        // top-level tables, so existing script instances — whose metatables
        // point at those tables — see the updated methods immediately.
        // Exposed to Lua as `Engine.ReloadStdlib()`.
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

        // Cached-bytecode -> live FScript pipeline. When the Out* params are
        // non-null they're populated by walking the freshly-loaded module —
        // used by LoadUniqueScriptPath on a cold cache to seed the entry.
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

        // Per-source-file cache of compiled bytecode + parsed schema + handler
        // keys. Built lazily on first LoadUniqueScriptPath for a given path,
        // dropped by InvalidateScriptCache when the source changes.
        THashMap<FName, FScriptCacheEntry> ScriptCache;
    };
    
}

#include "Scripting.inl"