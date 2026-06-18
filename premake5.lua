
-- Setup action runs without the engine tree; load it first and bail out before
-- workspace evaluation when invoked. (Setup.bat orchestrates `setup` -> `vs2022`.)
include "BuildScripts/Actions/Setup"
if _ACTION == "setup" then return end

include "BuildScripts/Dependencies"
include "BuildScripts/Workspace"
include "BuildScripts/Module"
include "BuildScripts/PluginDiscovery"
include "BuildScripts/Actions/Reflection"
include "BuildScripts/Actions/Clean"

-- Tests + GoogleTest are off by default (~22s clean-build cost); use `premake5 vs2022 --with-tests` to include them.
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

    group "Engine"
		include "Engine/Source/Runtime"
        include "Engine/Editor"
	group ""
    -- Sandbox is a STANDALONE game project (LuminaGameProject), not an engine module: it has its own
    -- Sandbox.sln via Engine/Sandbox/GenerateProject.bat and links the pre-built engine, exactly like a
    -- user's template project. So it is intentionally NOT included in the engine workspace here.

    -- The engine's managed C# API assembly as its own first-class C# project. premake can't generate
    -- an SDK-style net10 csproj (its C# support is legacy), so we author the .csproj and surface it
    -- here via externalproject; IDEs (Rider/VS) then treat it as a real C# project in the solution.
    group "Engine/Managed"
        -- The [NativeCall] source generator. A first-class solution project (not just a P2P analyzer
        -- reference) so MSBuild restores it on the solution and builds it in the SAME config it's
        -- resolved as an analyzer -- otherwise a .sln build builds it as bin/Debug while the analyzer
        -- path resolves elsewhere, and a fresh clone fails with CS0006 (the DLL "could not be found").
        externalproject "LuminaSharp.Generators"
            location "Engine/Source/LuminaSharp.Generators"
            uuid "B7E2D9C4-1A6F-4C3B-8D5E-2F9A0B1C3D4E"
            kind "SharedLib"
            language "C#"

        externalproject "LuminaSharp"
            location "Engine/Source/LuminaSharp"
            uuid "8F4B1C2A-3D4E-5F6A-7B8C-9D0E1F2A3B4C"
            kind "SharedLib"
            language "C#"
            -- Build after Runtime so the Reflector has emitted the generated C# bindings the csproj globs,
            -- and after the generator so the analyzer DLL exists when this compiles.
            dependson { "Runtime", "LuminaSharp.Generators" }
    group ""

    -- Included after Engine so Tests' ModuleDependencies (Runtime/Editor) are already registered and
    -- propagate their public include dirs (notably ModuleAPI.h) into the Tests project.
    if _OPTIONS["with-tests"] then
        group "Tests"
            include "Engine/Tests"
        group ""
    end

    -- Engine plugins drop in automatically: Engine/Plugins/<Name>/<Name>.lua becomes a project here, matching FPluginManager's startup walk.
    group "Plugins"
        LuminaDiscoverEnginePlugins()
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
        if LuminaOptions.IsActiveAny("Tracy") then
            include "Engine/Source/ThirdParty/Tracy"
        end
        include "Engine/Source/ThirdParty/MiniAudio"
        include "Engine/Source/ThirdParty/SPDLog"
        include "Engine/Source/ThirdParty/JoltPhysics"
        include "Engine/Source/ThirdParty/Recast"
        include "Engine/Source/ThirdParty/enet"
        include "Engine/Source/ThirdParty/RPMalloc"
        include "Engine/Source/ThirdParty/XXHash"
        include "Engine/Source/ThirdParty/miniz"
        include "Engine/Source/ThirdParty/VulkanMemoryAllocator"
        include "Engine/Source/ThirdParty/Volk"
        include "Engine/Source/ThirdParty/tinyobjloader"
        include "Engine/Source/ThirdParty/MeshOptimizer"
        include "Engine/Source/ThirdParty/MikkTSpace"
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
        include "Engine/Source/ThirdParty/msdfgen"
	group ""

    group "Build"
        include "BuildScripts"
        include "BuildScripts/ReflectionGen.lua"
    group ""