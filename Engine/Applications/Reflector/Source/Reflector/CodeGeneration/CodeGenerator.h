#pragma once
#include "Reflector/ReflectionCore/ReflectionDatabase.h"

namespace Lumina::Reflection
{
    class FReflectedWorkspace;
    class FReflectedDatabase;
    class FCodeWriter;

    /**
     * Top-level orchestrator. Walks the reflection database and produces, for each
     * header that contains reflected types:
     *   - <name>.generated.h   — public macros: forward decls, GENERATED_BODY, etc.
     *   - <name>.generated.cpp — the Construct_* statics, singletons, Lua bindings.
     * And per-project:
     *   - ReflectionUnity.gen.cpp        — #includes every .generated.cpp in the project.
     *   - Game/Scripts/Definitions/GlobalDefs.d.luau — Luau type definitions.
     */
    class FCodeGenerator
    {
    public:

        FCodeGenerator(FReflectedWorkspace* InWorkspace, const FReflectionDatabase& Database);

        void GenerateCode();

    private:

        void GenerateHeaderFile(FReflectedHeader* Header);
        void GenerateSourceFile(FReflectedHeader* Header);

        void WriteHeaderContent(FCodeWriter& Writer, FReflectedHeader* Header);
        void WriteSourceContent(FCodeWriter& Writer, FReflectedHeader* Header);
        void WriteLuaApiContent(FCodeWriter& Writer, FReflectedHeader* Header);

        void WriteUnityBuildFile(FReflectedProject* Project, const eastl::string& Contents);
        void WriteLuaDefinitionsFile(FReflectedProject* Project, const eastl::string& Contents);

    private:

        FReflectedWorkspace*       Workspace = nullptr;
        const FReflectionDatabase* ReflectionDatabase = nullptr;
    };
}
