
-- Setup action runs without the engine tree; load it first and bail out before
-- workspace evaluation when invoked. (Setup.bat orchestrates `setup` -> `vs2022`.)
include "BuildScripts/Actions/Setup"
if _ACTION == "setup" then return end

include "BuildScripts/Dependencies"
include "BuildScripts/Workspace"
include "BuildScripts/CSharpProject"
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
        -- The [NativeCall] Roslyn source generator. Fully premake-generated SDK-style csproj via the
        -- CSharpProject extension (premake's stock C# generator can't emit IsRoslynComponent etc.).
        project "LuminaSharp.Generators"
            location "Engine/Source/LuminaSharp.Generators"
            kind "SharedLib"
            language "C#"
            clr "NetCore"
            dotnetsdk "Default"
            dotnetframework "netstandard2.0"
            files { "Engine/Source/LuminaSharp.Generators/**.cs" }
            -- The **.cs glob is evaluated at generation time; keep MSBuild's own generated obj/ sources
            -- (AssemblyInfo/AssemblyAttributes) out or they duplicate the SDK's auto-generated ones.
            removefiles { "Engine/Source/LuminaSharp.Generators/obj/**", "Engine/Source/LuminaSharp.Generators/bin/**" }
            dotnetstripdefines(true)
            dotnetrawprops {
                "<LangVersion>latest</LangVersion>",
                "<Nullable>enable</Nullable>",
                "<ImplicitUsings>disable</ImplicitUsings>",
                "<IncludeBuildOutput>false</IncludeBuildOutput>",
                "<IsRoslynComponent>true</IsRoslynComponent>",
                "<EnforceExtendedAnalyzerRules>true</EnforceExtendedAnalyzerRules>",
            }
            -- PrivateAssets=all so the Roslyn package stays build-time only; injected directly to set the metadata.
            dotnetrawitems {
                [[<PackageReference Include="Microsoft.CodeAnalysis.CSharp" Version="4.13.0" PrivateAssets="all" />]],
            }

        -- The managed engine API (loaded at runtime by FDotNetHost). References the generator AS an
        -- analyzer and compiles the Reflector-emitted bindings.
        project "LuminaSharp"
            location "Engine/Source/LuminaSharp"
            kind "SharedLib"
            language "C#"
            clr "NetCore"
            dotnetsdk "Default"
            dotnetframework "net10.0"
            files { "Engine/Source/LuminaSharp/**.cs" }
            removefiles { "Engine/Source/LuminaSharp/obj/**", "Engine/Source/LuminaSharp/bin/**" }
            -- Build after Runtime (Reflector emits the bindings) and after the generator (analyzer DLL exists).
            dependson { "Runtime", "LuminaSharp.Generators" }
            -- Output straight into the engine binaries where FDotNetHost looks.
            targetdir(path.join(LuminaConfig.GetTargetDirectory(), "DotNet", "Managed"))
            dotnetstripdefines(true)
            dotnetrawprops {
                "<EnableDynamicLoading>true</EnableDynamicLoading>",
                "<AllowUnsafeBlocks>true</AllowUnsafeBlocks>",
                "<Nullable>enable</Nullable>",
                "<ImplicitUsings>disable</ImplicitUsings>",
                "<AssemblyName>LuminaSharp</AssemblyName>",
                "<RootNamespace>LuminaSharp</RootNamespace>",
                "<Deterministic>true</Deterministic>",
                "<AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>",
                "<AppendRuntimeIdentifierToOutputPath>false</AppendRuntimeIdentifierToOutputPath>",
            }
            dotnetrawitems {
                [[<PackageReference Include="Microsoft.CodeAnalysis.CSharp" Version="4.13.0" />]],
                -- The generator as a source-generator/analyzer reference (the wire premake can't make).
                [[<ProjectReference Include="..\LuminaSharp.Generators\LuminaSharp.Generators.csproj" OutputItemType="Analyzer" ReferenceOutputAssembly="false" />]],
                -- Reflector-emitted C# bindings (MSBuild glob, expanded at build time -- they don't exist at premake time).
                [[<Compile Include="..\..\..\Intermediates\CSharpBindings\**\*.generated.cs"><Link>Generated\%(RecursiveDir)%(Filename)%(Extension)</Link></Compile>]],
            }
            -- Copy the generator assembly next to LuminaSharp.dll so the runtime ScriptCompiler can load it.
            dotnetrawtail {
                [[<Target Name="CopyGeneratorForRuntime" AfterTargets="Build">]],
                [[  <Copy SourceFiles="@(Analyzer)" DestinationFolder="$(OutputPath)" SkipUnchangedFiles="true" Condition="'%(Filename)' == 'LuminaSharp.Generators'" />]],
                [[</Target>]],
            }
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