-- Single Utility project that runs the Reflector prebuild once per build; every Reflection=true module dependson it instead of paying premake startup per module.
-- fastuptodate "Off" forces VS to run the PreBuildEvent every build (reflected headers aren't tracked inputs); the lua action then dirty-checks libclang internally.

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
