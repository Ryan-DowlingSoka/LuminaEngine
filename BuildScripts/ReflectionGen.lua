-- Single Utility project running the Reflector prebuild once per build; Reflection=true modules dependon it.
-- fastuptodate "Off" required: reflected headers aren't tracked inputs, so force the prebuild every build (lua action dirty-checks internally).

project "ReflectionGen"
    kind "Utility"
    fastuptodate "Off"
    targetdir(LuminaConfig.GetTargetDirectory())
    objdir(path.join(LuminaConfig.EnginePath("Intermediates/Obj"),
                     LuminaConfig.OutputDirectory, "ReflectionGen"))

    -- The Reflector binary must build first or reflection races on a clean build.
    dependson { "Reflector" }

    files { "../ReflectionRunner.bat", "../Actions/Reflection.lua" }

    prebuildcommands
    {
        LuminaConfig.RunReflection()
    }
