premake.api.register
{
    name    = "enablereflection",
    scope   = "project",
    kind    = "boolean",
}

-- Destination for this project's generated C# bindings (.generated.cs). Empty/unset = the engine default
-- (Intermediates/CSharpBindings -> LuminaSharp.dll). A plugin/game module sets it to its Scripts/Generated
-- so the per-plugin gather compiles the bindings into the plugin's OWN assembly.
premake.api.register
{
    name    = "csharpbindingsdir",
    scope   = "project",
    kind    = "string",
}

LuminaConfig = LuminaConfig or {}

function capitalize(str)
    if not str or str == "" then
        return ""
    end
    return str:sub(1, 1):upper() .. str:sub(2)
end

ArchBits = 
{
    ["x86"]     = "32",
    ["x86_64"]  = "64",
    ["ARM64"]   = "ARM64"
}

-- Prefer LUMINA_DIR so EnginePath() resolves to the engine install; the %{wks.location} fallback is correct for engine builds but wrong for game projects.
local _LuminaEnvDir = os.getenv("LUMINA_DIR")
LuminaConfig.EngineDirectory        = _LuminaEnvDir or "%{wks.location}"
LuminaConfig.OutputDirectory        = "%{capitalize(cfg.system)}%{ArchBits[cfg.architecture]}"
LuminaConfig.ProjectFilesDirectory  = "%{wks.location}/Intermediates/ProjectFiles/%{prj.name}"
LuminaConfig.ReflectionDirectory    = "%{wks.location}/Intermediates/Reflection/%{prj.name}"

-- An engine root contains "Engine/Source/Runtime"; a game project's _MAIN_SCRIPT_DIR does not. Used to tell the two apart.
local function LooksLikeEngineRoot(Dir)
    if not Dir or Dir == "" then return false end
    return os.isdir(path.join(Dir, "Engine/Source/Runtime"))
end

if not _LuminaEnvDir then
    if LooksLikeEngineRoot(_MAIN_SCRIPT_DIR) then
        -- Engine build with no env var set: harmless, the wks.location fallback resolves correctly.
    else
        error(table.concat({
            "",
            "LUMINA_DIR is not set, and this workspace (" .. _MAIN_SCRIPT_DIR .. ")",
            "does not look like an engine root. For game projects, LUMINA_DIR must",
            "point at the Lumina engine install. Run the engine's Setup.bat first,",
            "or set LUMINA_DIR manually (setx LUMINA_DIR \"C:\\path\\to\\lumina\").",
        }, "\n"))
    end
elseif not LooksLikeEngineRoot(_LuminaEnvDir) then
    error(table.concat({
        "",
        "LUMINA_DIR points at '" .. _LuminaEnvDir .. "',",
        "which does not contain Engine/Source/Runtime. This usually means the engine",
        "was moved or deleted after Setup.bat ran. Re-run Setup.bat from the engine",
        "root, or fix LUMINA_DIR (setx LUMINA_DIR \"C:\\path\\to\\lumina\").",
    }, "\n"))
end

function LuminaConfig.GetSystem()
    return "%{capitalize(cfg.system)}"
end

function LuminaConfig.GetSharedLibExtName()
    if os.host() == "windows" then
        return ".dll"
    elseif os.host() == "linux" then
        return ".so"
    elseif os.host() == "macosx" then
        return ".dylib"
    end

    return ""
end

function LuminaConfig.GetArchitecture()
    return "%{ArchBits[cfg.architecture]}"
end

function LuminaConfig.GetTargetDirectory()
    return path.join(LuminaConfig.EngineDirectory, "Binaries", LuminaConfig.OutputDirectory)
end

function LuminaConfig.GetProjectName()
    return "%{prj.name}"
end

function LuminaConfig.GetObjDirectory()
    return path.join(LuminaConfig.EngineDirectory, "Intermediates", "Obj", LuminaConfig.OutputDirectory, LuminaConfig.GetProjectName())
end

function LuminaConfig.ThirdPartyDirectory()
    return path.join(LuminaConfig.EngineDirectory, "Engine/Source/ThirdParty")
end

function LuminaConfig.EnginePath(Subpath)
    return path.join(LuminaConfig.EngineDirectory, Subpath)
end

function LuminaConfig.ThirdPartyPath(Subpath)
    return path.join(LuminaConfig.EngineDirectory, "Engine/Source/ThirdParty", Subpath)
end

function LuminaConfig.GetCPPFilesInDirectory(Path)
    return os.matchfiles(path.join(Path, "**.cpp"))
end

function LuminaConfig.Execute(Command, ...)
    local args = {...}
    local quote = function(s) return "\"" .. s .. "\"" end

    for i, v in ipairs(args) do
        args[i] = quote(v)
    end

    local cmd = Command .. " " .. table.concat(args, " ")

    table.insert(LuminaConfig.PreBuildCommands, cmd)

    return cmd
end

function LuminaConfig.WorkspacePath(Subpath)
    return path.join("%{wks.location}", Subpath)
end

function LuminaConfig.ProjectPath(Subpath)
    return path.join("%{prj.location}", Subpath)
end

function LuminaConfig.GetReflectionFiles()
    return path.join(LuminaConfig.ReflectionDirectory, "ReflectionUnity.gen.cpp")
end

-- Canonical EASTL allocator binding; every EASTL-using image needs its own compiled copy (one eastl::allocator definition per image). Module.lua/GameProject.lua auto-add it.
function LuminaConfig.GetEASTLImplFile()
    return LuminaConfig.EnginePath("Engine/Source/Runtime/Memory/EASTLImpl.cpp")
end

-- Engine headers every consumer needs: the Runtime tree and Runtime's generated reflection headers, resolved against the engine install via EnginePath().
function LuminaConfig.GetEngineRuntimeIncludes()
    return
    {
        LuminaConfig.EnginePath("Engine/Source/Runtime"),
        LuminaConfig.EnginePath("Intermediates/Reflection/Runtime"),
    }
end

function LuminaConfig.GetEngineEditorIncludes()
    return
    {
        LuminaConfig.EnginePath("Engine/Editor/Source"),
        LuminaConfig.EnginePath("Intermediates/Reflection/Editor"),
    }
end

function LuminaConfig.CopyFile(Source, Destination)
    if not Source or not Destination then
        error("CopyFile requires source and Destination")
    end

    local Quote =  function(s) return "\"" .. s .. "\"" end

    return "{COPYFILE} " .. Quote(Source) .. " " .. Quote(Destination)
end

-- Like CopyFile but never fails; for stable third-party DLLs (slang, aftermath) that a running editor may lock during a Game build (same version anyway).
function LuminaConfig.CopyFileIgnoreErrors(Source, Destination)
    if not Source or not Destination then
        error("CopyFileIgnoreErrors requires source and Destination")
    end

    local Quote = function(s) return "\"" .. s .. "\"" end

    return "{COPYFILE} " .. Quote(Source) .. " " .. Quote(Destination) .. " || ver >NUL"
end

function LuminaConfig.MakeDirectory(Path)

    local Quote =  function(s) return "\"" .. s .. "\"" end
    return "{MKDIR} " .. Quote(Path)
end

function LuminaConfig.RunReflection()

    return path.join(LuminaConfig.EngineDirectory, "BuildScripts/ReflectionRunner.bat")
end

-- Per-dependency third-party metadata. Modules pull a library's includes/links
-- by naming it in Dependencies; there is no global include dump.
include(path.join(_SCRIPT_DIR, "ThirdParty.lua"))

-- Optional-feature toggles; resolves BuildConfig.lua + CLI flags into the LuminaOptions query API used to gate defines, links, and DLL copies.
include(path.join(_SCRIPT_DIR, "Options.lua"))