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
-- Utility kind: produces no build output, so MSBuild has no
-- input/output FastUpToDateCheck to skip on — prebuild fires every build,
-- which is what we want. The lua action handles "do we actually need to
-- run libclang" internally.

project "ReflectionGen"
    kind "Utility"
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
