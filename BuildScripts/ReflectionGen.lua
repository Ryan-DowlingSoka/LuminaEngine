-- ReflectionGen
--
-- A single Utility project that runs the Reflector prebuild for the entire
-- workspace. Every Reflection=true module `dependson` this project, so the
-- reflection pipeline kicks off exactly once per build instead of once per
-- reflected module.
--
-- Why this matters: the lua-side `Reflection` action already does its own
-- cross-project dirty check and exits cheaply when nothing changed, but
-- each invocation pays premake's startup + workspace-walk cost (~900ms).
-- With Runtime, Editor and Sandbox all firing the prebuild that was ~2.7s
-- wasted on every incremental build. A single Utility project collapses
-- that to one premake run.
--
-- Utility kind produces no build output, but that alone does NOT make the
-- prebuild fire every build: VS's FastUpToDateCheck still evaluates this
-- project, and since reflected headers (Mesh.h, etc.) are not among its
-- tracked inputs, editing one leaves ReflectionGen "up-to-date" and the
-- prebuild gets skipped entirely. `fastuptodate "Off"`
-- (DisableFastUpToDateCheck) forces VS to invoke MSBuild every build so the
-- PreBuildEvent always runs. The lua action then handles "do we actually
-- need to run libclang" internally via its own cross-project dirty check.

project "ReflectionGen"
    kind "Utility"
    fastuptodate "Off"
    targetdir(LuminaConfig.GetTargetDirectory())
    objdir(path.join(LuminaConfig.EnginePath("Intermediates/Obj"),
                     LuminaConfig.OutputDirectory, "ReflectionGen"))

    -- The Reflector binary must exist before reflection can run. Skipping
    -- this dependency would race on first build.
    dependson { "Reflector" }

    files { "../ReflectionRunner.bat", "../Actions/Reflection.lua" }

    prebuildcommands
    {
        LuminaConfig.RunReflection()
    }
