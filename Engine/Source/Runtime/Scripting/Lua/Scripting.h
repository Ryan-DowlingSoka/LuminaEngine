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

    // Result of a single luau_compile attempt. Line is 1-based as Luau
    // reports it (begin.line + 1); -1 means the message had no parsable
    // location prefix.
    struct FCompileDiagnostic
    {
        FString Path;
        FString Message;
        int     Line   = -1;
    };

    DECLARE_MULTICAST_DELEGATE(FScriptCompileErrorDelegate, FStringView /*Path*/, const FCompileDiagnostic& /*Diag*/);
    DECLARE_MULTICAST_DELEGATE(FScriptCompileSuccessDelegate, FStringView /*Path*/);

    // Per-path cache; subsequent loads skip file read + luau_compile + schema build.
    struct FScriptCacheEntry
    {
        TVector<uint8>                  Bytecode;
        FScriptExportSchema             ExportsSchema;
        TVector<FScriptPropertyEntry>   ExportDefaults;
    };

    // require() result cache. Bytecode is held C++-side so reload re-reads VFS;
    // the module value is pinned via lua_ref so it survives GC for the lifetime
    // of the cache entry. RegistryRef==LUA_NOREF means "compiled but not yet executed".
    struct FModuleCacheEntry
    {
        TVector<uint8>  Bytecode;
        int             RegistryRef = -1; // LUA_NOREF
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

        // require("Foo/Bar") resolution + load. Internal entry point for the C `require`
        // function bound onto every lua_State. Caller is responsible for handling stack.
        // Returns true on success and leaves the module result on top of `Thread`.
        bool RequireModule(lua_State* Thread, FStringView ModuleName);

        // Drops a require() module's cached bytecode AND its pinned result so the next
        // require() re-reads + re-runs the file. If the module returned a table and was
        // already cached, the table identity is preserved (new keys are copied into the
        // existing table) so any script that captured the module reference sees the
        // updated functions on its next call.
        RUNTIME_API void ReloadModule(FStringView ModuleName);

        // Walks LUA_GLOBALSINDEX one level deep for editor autocomplete.
        RUNTIME_API void HarvestGlobalSymbols(TVector<FLuaSymbol>& Out);

        // Re-runs Stdlib *.luau against the live VM; `Foo = Foo or {}` keeps existing instance metatables hot.
        // Exposed as `Engine.ReloadStdlib()`.
        RUNTIME_API void ReloadStdlib();
        
        #if LUAI_GCMETRICS
        RUNTIME_API const GCMetrics* GetGCMetrics() const;  
        #endif
        
        FScriptTransactionDelegate    OnScriptLoaded;
        FScriptTransactionDelegate    OnScriptDeleted;

        // Fires whenever a compile attempt for a known path either succeeds
        // or fails. The editor uses these to clear/set inline error markers
        // without having to poll the bytecode cache.
        FScriptCompileErrorDelegate   OnScriptCompileError;
        FScriptCompileSuccessDelegate OnScriptCompileSuccess;

    public:
        
        RUNTIME_API lua_State* GetVM() const { return L; }

    private:

        // Resolves a `require()` argument against the engine's VFS-backed module roots.
        // Returns true and writes the resolved virtual path on success.
        bool ResolveModulePath(FStringView ModuleName, FString& OutPath) const;

        // Compile + run module on the given thread, write the returned value into the
        // module cache as a Lua registry ref, and leave the value on top of the stack.
        // If `ExistingTable` is non-zero, copies new module keys into that table (kept
        // for hot-reload identity preservation) and returns that table instead.
        bool LoadModuleOntoThread(lua_State* Thread, const FString& ResolvedPath, int ExistingTableRegistryRef);

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

        THashMap<FName, FModuleCacheEntry> ModuleCache;
    };
    
}

#include "Scripting.inl"