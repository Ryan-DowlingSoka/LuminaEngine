
-- Setup action runs without the engine tree; load it first and bail out before
-- workspace evaluation when invoked. (Setup.bat orchestrates `setup` -> `vs2022`.)
include "BuildScripts/Actions/Setup"
if _ACTION == "setup" then return end

include "BuildScripts/Dependencies"
include "BuildScripts/Workspace"
include "BuildScripts/Module"
include "BuildScripts/Actions/Reflection"
include "BuildScripts/Actions/Clean"

-- Tests (and its GoogleTest dependency) are off by default so a normal
-- `premake5 vs2022` solution doesn't pay GoogleTest's ~22s clean-build cost.
-- Run `premake5 vs2022 --with-tests` to include them.
newoption {
    trigger     = "with-tests",
    description = "Include the Tests project and its GoogleTest dependency",
}

LuminaWorkspaceSettings({
    Name         = "Lumina",
    StartProject = "Lumina",
    TargetDir    = LuminaConfig.GetTargetDirectory(),
    ObjDir       = LuminaConfig.GetObjDirectory(),
})

    if _OPTIONS["with-tests"] then
        group "Tests"
            include "Engine/Tests"
        group ""
    end

    group "Engine"
		include "Engine/Source/Runtime"
        include "Engine/Editor"
        include "Engine/Sandbox"
	group ""

    group "Applications"
    	include "Engine/Applications/Lumina"
		include "Engine/Applications/Reflector"
	group ""

    group "Engine/Shaders"
        include "Engine/Resources/Shaders"
    group ""

	group "Engine/ThirdParty"
        include "Engine/Source/ThirdParty/EA"
        include "Engine/Source/ThirdParty/EnTT"
		include "Engine/Source/ThirdParty/glfw"
		include "Engine/Source/ThirdParty/imgui"
        -- Tracy is dropped entirely when profiling is disabled in every
        -- configuration (BuildConfig.lua / --tracy=off); modules also stop
        -- linking it (see Module.lua).
        if LuminaOptions.IsActiveAny("Tracy") then
            include "Engine/Source/ThirdParty/Tracy"
        end
        include "Engine/Source/ThirdParty/MiniAudio"
        include "Engine/Source/ThirdParty/EnkiTS"
        include "Engine/Source/ThirdParty/Luau"
        include "Engine/Source/ThirdParty/SPDLog"
        include "Engine/Source/ThirdParty/JoltPhysics"
        include "Engine/Source/ThirdParty/Recast"
        include "Engine/Source/ThirdParty/RPMalloc"
        include "Engine/Source/ThirdParty/XXHash"
        include "Engine/Source/ThirdParty/miniz"
        include "Engine/Source/ThirdParty/VulkanMemoryAllocator"
        include "Engine/Source/ThirdParty/Volk"
        include "Engine/Source/ThirdParty/tinyobjloader"
        include "Engine/Source/ThirdParty/MeshOptimizer"
        include "Engine/Source/ThirdParty/MikkTSpace"
        include "Engine/Source/ThirdParty/vk-bootstrap"
        include "Engine/Source/ThirdParty/json"
        include "Engine/Source/ThirdParty/fastgltf"
        include "Engine/Source/ThirdParty/OpenFBX"
        include "Engine/Source/ThirdParty/basis_universal"
        include "Engine/Source/ThirdParty/SLang"
        if _OPTIONS["with-tests"] then
            include "Engine/Source/ThirdParty/GoogleTest"
        end
        include "Engine/Source/ThirdParty/FreeType"
        include "Engine/Source/ThirdParty/RmlUi"
	group ""

    group "Build"
        include "BuildScripts"
        include "BuildScripts/ReflectionGen.lua"
    group ""