
include "BuildScripts/Dependencies"
include "BuildScripts/Module"
include "BuildScripts/Actions/Reflection"

workspace "Lumina"
	language "C++"
	cppdialect "C++latest"
	--staticruntime "Off"
    warnings "Default"
    targetdir (LuminaConfig.GetTargetDirectory())
    objdir (LuminaConfig.GetObjDirectory())
    enableunitybuild "Off"
    fastuptodate "On"
    multiprocessorcompile "On"
	startproject "Lumina"

    configurations 
    { 
        "Debug",
        "Development",
        "Shipping",
    }

    platforms
    {
        "Editor",
        "Game",
    }

    defaultplatform ("Editor")

    filter "configurations:Debug"
        defines
        {
            "JPH_DEBUG_RENDERER",
            "JPH_FLOATING_POINT_EXCEPTIONS_ENABLED",
            "JPH_EXTERNAL_PROFILE",
            "JPH_ENABLE_ASSERTS",
            "LUAI_GCMETRICS",
        }
    filter {}
		
	defines
	{
        "JPH_OBJECT_LAYER_BITS=32",
        "EASTL_USER_DEFINED_ALLOCATOR=1",
		"_CRT_SECURE_NO_WARNINGS",
        "_SILENCE_CXX23_ALIGNED_UNION_DEPRECATION_WARNING",
        "_SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING",
		"GLM_FORCE_DEPTH_ZERO_TO_ONE",
		"GLM_FORCE_LEFT_HANDED",
        "GLM_ENABLE_EXPERIMENTAL",
		"IMGUI_DEFINE_MATH_OPERATORS",
        "IMGUI_IMPL_VULKAN_USE_VOLK",
        
        "LUA_VECTOR_SIZE=4",
        "LUA_UTAG_LIMIT=1000",
        "LUA_LUTAG_LIMIT=1000",

        "TRACY_ALLOW_SHADOW_WARNING",
        "TRACY_ENABLE",
    	"TRACY_CALLSTACK",
    	"TRACY_ON_DEMAND",
        
        'LUMINA_SYSTEM_NAME=\"%{LuminaConfig.GetSystem()}\"',
        'LUMINA_ARCH_NAME=\"%{LuminaConfig.GetArchitecture()}\"',
        'LUMINA_CONFIGURATION_NAME=\"%{cfg.buildcfg}\"',
        'LUMINA_PLATFORM_NAME=\"%{LuminaConfig.GetSystem()}%{LuminaConfig.GetArchitecture()}\"',
        'LUMINA_SHAREDLIB_EXT_NAME=\"%{LuminaConfig.GetSharedLibExtName()}\"',
	}

	disablewarnings
    {
        "4251", -- DLL-interface warning
        "4275", -- Non-DLL-interface base class
        "4244", -- "Precision loss warnings"
        "4267", -- "Precision loss warnings"
	}

    filter "kind:SharedLib"
        defines { "%{prj.name:upper()}_EXPORTS"}
    filter {}

    filter "language:C++ or language:C"
		architecture "x86_64"
        
    filter "architecture:64"
        defines { "LUMINA_PLATFORM_CPU_X86_64" }

    filter "system:windows"
        systemversion "latest"
        conformancemode "On"
        defines 
        { 
            "LE_PLATFORM_WINDOWS",
            "DLL_EXPORT=__declspec(dllexport)",
            "DLL_IMPORT=__declspec(dllimport)",
        }
        buildoptions 
        {
            "/Zc:preprocessor",
            "/Zc:inline",
            "/Zc:__cplusplus",
            "/bigobj"
        }

    filter {}
    
    filter "platforms:Game"
        defines { "WITH_EDITOR=0" }

    filter "platforms:Editor"
        defines { "WITH_EDITOR=1" }

    filter "configurations:Debug"
        targetsuffix "-Debug"
        linktimeoptimization "Off"
        incrementallink "On"
        optimize "Off"
        symbols "On"
        runtime "Debug"
        editandcontinue "On"
        defines { "LE_DEBUG", "LUMINA_DEBUG", "_DEBUG", "DEBUG", }

    filter "configurations:Development"
        targetsuffix "-Development"
        optimize "Speed"
        symbols "On"
        runtime "Release"
        linktimeoptimization "On"
        defines { "NDEBUG", "LE_DEVELOPMENT", "LUMINA_DEVELOPMENT", }

    filter "configurations:Shipping"
        linktimeoptimization "On"
        optimize "Full"
        symbols "Off"
        runtime "Release"
        defines { "NDEBUG", "LE_SHIPPING", "LUMINA_SHIPPING" }
        removedefines { "TRACY_ENABLE" }
    
    filter {}

    group "Tests"
        include "Engine/Tests"
    group ""

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
		include "Engine/Source/ThirdParty/Tracy"
        include "Engine/Source/ThirdParty/MiniAudio"
        include "Engine/Source/ThirdParty/EnkiTS"
        include "Engine/Source/ThirdParty/Luau"
        include "Engine/Source/ThirdParty/SPDLog"
        include "Engine/Source/ThirdParty/JoltPhysics"
        include "Engine/Source/ThirdParty/RPMalloc"
        include "Engine/Source/ThirdParty/XXHash"
        include "Engine/Source/ThirdParty/VulkanMemoryAllocator"
        include "Engine/Source/ThirdParty/Volk"
        include "Engine/Source/ThirdParty/tinyobjloader"
        include "Engine/Source/ThirdParty/MeshOptimizer"
        include "Engine/Source/ThirdParty/vk-bootstrap"
        include "Engine/Source/ThirdParty/SPIRV-Reflect"
        include "Engine/Source/ThirdParty/json"
        include "Engine/Source/ThirdParty/fastgltf"
        include "Engine/Source/ThirdParty/OpenFBX"
        include "Engine/Source/ThirdParty/basis_universal"
        include "Engine/Source/ThirdParty/SLang"
        include "Engine/Source/ThirdParty/GoogleTest"
	group ""

    group "Build"
        include "BuildScripts"
    group ""