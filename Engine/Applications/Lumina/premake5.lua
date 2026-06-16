LuminaModule({
    Name = "Lumina",
    Kind = "WindowedApp",
    ModuleDependencies = { "Runtime", },
    EditorModuleDependencies = { "Editor" },
})

-- LuminaSharp.dll (the managed C# API) is loaded at RUNTIME by this exe; it's NOT a C++ link dependency,
-- so a "build/run startup project" (F5/Play) builds only the C++ chain and would skip it -> C# scripting
-- disabled, [NativeCall] unresolved. This edge puts the managed project in the app's solution build graph
-- (correct build order + restore) so building/running Lumina always builds it. A solution clean/reload is
-- needed once after regenerating for the IDE to pick up the new dependency.
dependson { "LuminaSharp" }

-- Monolithic Shipping: every module is StaticLib linked here with /WHOLEARCHIVE so its FStaticModuleRegistration ctor survives the linker.
filter "configurations:Shipping"
    local Force = {}
    local Seen = {}

    local function ForceLink(ModuleName)
        if Seen[ModuleName] then return end
        Seen[ModuleName] = true
        table.insert(Force, "/WHOLEARCHIVE:" .. ModuleName .. "-Shipping.lib")
    end

    local function HasRemovedGamePlatform(Mod)
        for _, P in ipairs(Mod.RemovePlatforms or {}) do
            if P == "Game" then return true end
        end
        return false
    end

    -- Engine modules; skip Lumina (the exe), non-SharedLib projects, and Game-platform-removed modules (e.g. Editor).
    for Name, Mod in pairs(LuminaModules) do
        if Name ~= "Lumina"
            and Mod.Kind == "SharedLib"
            and not HasRemovedGamePlatform(Mod) then
            ForceLink(Name)
            links { Name } -- adds to AdditionalDependencies + puts lib dir on LIBPATH
        end
    end

    -- Plugin modules. Editor-only modules are removeplatforms { "Game" }.
    for _, Plugin in pairs(LuminaPlugins) do
        for _, Mod in ipairs(Plugin.Modules) do
            if Mod.Type ~= "Editor" then
                ForceLink(Mod.Name)
                links { Mod.Name }
            end
        end
    end

    if #Force > 0 then
        linkoptions(Force)
    end

    -- In Shipping, Module.lua strips per-module third-party links to avoid duplicate .objs; Lumina unions all deps and links them directly.
    local AllThirdPartyDeps = {}
    local SeenDep = {}
    local function AddDeps(Deps)
        for _, D in ipairs(Deps or {}) do
            if not SeenDep[D] then
                SeenDep[D] = true
                table.insert(AllThirdPartyDeps, D)
            end
        end
    end
    for Name, Mod in pairs(LuminaModules) do
        if Name ~= "Lumina"
            and Mod.Kind == "SharedLib"
            and not HasRemovedGamePlatform(Mod) then
            AddDeps(Mod.Dependencies)
        end
    end
    for _, Plugin in pairs(LuminaPlugins) do
        for _, Mod in ipairs(Plugin.Modules) do
            if Mod.Type ~= "Editor" then
                AddDeps(Mod.AllDependencies or Mod.Dependencies)
            end
        end
    end
    local _, _, ThirdPartyLinks = LuminaThirdParty.Resolve(AllThirdPartyDeps)
    if #ThirdPartyLinks > 0 then
        links(ThirdPartyLinks)
    end
filter {}
