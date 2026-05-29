premake.api.register 
{
    name    = "enablereflection",
    scope   = "project",
    kind    = "boolean",
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

-- External game projects set LUMINA_DIR env var; use it when present so that
-- all EnginePath() calls resolve to the actual engine install rather than the
-- game project's workspace location.
local _LuminaEnvDir = os.getenv("LUMINA_DIR")
LuminaConfig.EngineDirectory        = _LuminaEnvDir or "%{wks.location}"
LuminaConfig.OutputDirectory        = "%{capitalize(cfg.system)}%{ArchBits[cfg.architecture]}"
LuminaConfig.ProjectFilesDirectory  = "%{wks.location}/Intermediates/ProjectFiles/%{prj.name}"
LuminaConfig.ReflectionDirectory    = "%{wks.location}/Intermediates/Reflection/%{prj.name}"

if not LuminaConfig.EngineDirectory then
    error("LUMINA_DIR environment variable not set. Run Setup.bat first.")
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

-- Absolute path to the single canonical EASTL allocator binding. Every linked
-- image that uses EASTL containers needs its own compiled copy of this file
-- (eastl::allocator is non-template / non-inline; one definition per image).
-- Module.lua and GameProject.lua auto-add it to consumer compile sets so the
-- user never has to ship boilerplate next to their own source.
function LuminaConfig.GetEASTLImplFile()
    return LuminaConfig.EnginePath("Engine/Source/Runtime/Memory/EASTLImpl.cpp")
end

-- Engine module headers a consumer always needs regardless of which
-- third-party libs it pulls in: the Runtime tree (ModuleAPI.h + public
-- headers) and Runtime's generated reflection headers. External game projects
-- resolve these against the engine install via EnginePath().
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

-- Same as CopyFile but suppresses failure (returns success even if the copy
-- bombed). Use this for stable third-party DLLs (slang, aftermath) that may
-- be locked by an already-running editor when packaging triggers a Game
-- build. The locked DLL is the same version we'd be copying anyway, so
-- silently succeeding is the right behavior.
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

-- Optional-feature toggles (Tracy / Vulkan validation / NVIDIA Aftermath).
-- Resolves BuildConfig.lua + CLI flags into the LuminaOptions query API used by
-- Workspace.lua and the module build to gate defines, links and DLL copies.
include(path.join(_SCRIPT_DIR, "Options.lua"))