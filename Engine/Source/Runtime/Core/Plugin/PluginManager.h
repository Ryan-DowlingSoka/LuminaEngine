#pragma once

#include "Plugin.h"
#include "PluginLoadingPhase.h"
#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Containers/String.h"
#include "Memory/SmartPtr.h"

namespace Lumina
{
    // Per-project override of plugin enabled state. Parsed out of the
    // .lproject's "Plugins" array (see Engine::LoadProject). Each entry can
    // force a plugin on or off regardless of its descriptor default.
    struct FProjectPluginOverride
    {
        FString Name;
        bool    bEnabled = true;
    };

    // Discovers .lplugin files under Engine/Plugins/ and <Project>/Plugins/,
    // resolves the dependency graph, mounts content into the VFS, and loads
    // each plugin's module DLLs at the engine bring-up phase declared in
    // each module's descriptor.
    //
    // Discovery is split in two passes: engine plugins are scanned at the
    // top of FEngine::Init so their Earliest/Core modules can sit ahead of
    // even the renderer; project plugins are scanned inside Engine::LoadProject
    // after the .lproject is parsed (PostProjectLoad and earlier project-plugin
    // phases dispatch immediately after discovery).
    class RUNTIME_API FPluginManager
    {
    public:
        // Mark non-copyable so MSVC doesn't synthesize the copy ctor, which
        // would force eager instantiation of OwnedPlugins's vector copy ctor
        // and fail on TUniquePtr's deleted copy.
        LE_NO_COPYMOVE(FPluginManager);

        static FPluginManager& Get();

        // Walk Engine/Plugins/ (relative to Paths::GetEngineDirectory()) and
        // build the engine-plugin registry. Idempotent; safe to call twice.
        void DiscoverEnginePlugins();

        // Walk <ProjectDirectory>/Plugins/ and add any project plugins.
        // Project plugins layer on top of engine plugins and may depend on
        // them. Idempotent for the same project root.
        void DiscoverProjectPlugins(FStringView ProjectDirectory);

        // Apply per-project enable/disable overrides. Called once
        // immediately after DiscoverProjectPlugins from LoadProject.
        void ApplyProjectOverrides(const TVector<FProjectPluginOverride>& Overrides);

        // Drive the bring-up of every enabled plugin module registered to
        // this phase. Modules that have already been loaded (e.g. because
        // their phase was Earliest and we're now running Core) are skipped.
        void LoadModulesForPhase(EPluginLoadingPhase Phase);

        // Reverse-order shutdown of every loaded plugin module. Called from
        // FEngine::Shutdown before the module manager unloads anything else.
        void ShutdownAllPlugins();

        FPlugin*               FindPlugin(FStringView Name);
        const FPlugin*         FindPlugin(FStringView Name) const;
        TVector<FPlugin*>      GetEnabledPlugins();
        TVector<const FPlugin*> GetAllPlugins() const;

    private:
        FPluginManager() = default;
        ~FPluginManager() = default;

        // Discover all .lplugin files under Root and add them to the
        // registry. bIsEngine flags whether the descriptors should be
        // tagged as engine plugins for later filtering.
        void DiscoverDirectory(FStringView Root, bool bIsEngine);

        // Load one plugin module: resolves the binary path, calls into
        // FModuleManager, calls StartupModule, records the result on the
        // plugin's loaded-module list.
        bool LoadPluginModule(FPlugin& Plugin, const FPluginModuleDescriptor& Module);

        // Mount /PluginName into the VFS. Skipped if !ContainsContent or
        // the Content/ dir does not exist on disk.
        void MountPluginContent(FPlugin& Plugin);

        // Filter: false means "do not load this module in this build". Drops
        // editor modules in non-editor builds, developer modules in
        // shipping, and modules whose SupportedPlatforms exclude us.
        bool IsModuleApplicable(const FPluginModuleDescriptor& Module) const;

        // Filter on the plugin level: EditorOnly + non-editor build skips
        // the whole plugin, SupportedPlatforms misses ditto.
        bool IsPluginApplicable(const FPluginDescriptor& Desc) const;

        // Topological sort of the enabled-plugin set so dependents load
        // after their dependencies. Order is deterministic: peer plugins
        // (no dep edges between them) come out in OwnedPlugins insertion
        // order, which matches descriptor discovery order. Cycles are
        // logged with the participating plugin names and the cycle's
        // members fall back to insertion order so the run is still
        // reproducible.
        TVector<FPlugin*> BuildLoadOrder();

    private:
        // Ownership is held in the vector; the map is for O(1) name lookup.
        // EASTL's hash_map eagerly instantiates the value type's copy ctor
        // (a known quirk), so move-only TUniquePtr can't live as a map value.
        TVector<TUniquePtr<FPlugin>> OwnedPlugins;
        THashMap<FName, FPlugin*>    PluginLookup;

        // Cache of the topological order so LoadModulesForPhase doesn't
        // resort on every phase. Invalidated by discovery / overrides.
        TVector<FPlugin*> CachedLoadOrder;
        bool              bLoadOrderDirty = true;

        FString           ProjectDirectory; // last DiscoverProjectPlugins arg
    };
}
