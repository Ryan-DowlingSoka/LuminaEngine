LuminaModule({
    Name = "Lumina",
    Kind = "WindowedApp",
    ModuleDependencies = { "Runtime", },
    EditorModuleDependencies = { "Editor" },
})

-- Runtime-only dep, not a C++ link; keep it in the build graph or F5/Play skips it and C# scripting silently dies.
dependson { "LuminaSharp" }

-- Monolithic Shipping: link every module with /WHOLEARCHIVE or its FStaticModuleRegistration ctor gets dropped.
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

    -- Engine modules; skip the exe, non-SharedLib, and Game-removed modules (e.g. Editor).
    for Name, Mod in pairs(LuminaModules) do
        if Name ~= "Lumina"
            and Mod.Kind == "SharedLib"
            and not HasRemovedGamePlatform(Mod) then
            ForceLink(Name)
            links { Name }
        end
    end

    -- Plugin modules; editor-only ones are removeplatforms { "Game" }.
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

    -- Shipping strips per-module third-party links (dup .objs); union all deps and link them here.
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
