#pragma once
#include "Reflector/ReflectionCore/ReflectionDatabase.h"

namespace Lumina::Reflection
{
    class FReflectedWorkspace;
    class FReflectedDatabase;
    class FCodeWriter;
    
    class FCodeGenerator
    {
    public:

        FCodeGenerator(FReflectedWorkspace* InWorkspace, const FReflectionDatabase& Database);

        void GenerateCode();

    private:

        void GenerateHeaderFile(FReflectedHeader* Header);
        void GenerateSourceFile(FReflectedHeader* Header);
        void GenerateCSharpFile(FReflectedHeader* Header);

        void WriteHeaderContent(FCodeWriter& Writer, FReflectedHeader* Header);
        void WriteSourceContent(FCodeWriter& Writer, FReflectedHeader* Header);

        // Returns true if any C#-exposed type was emitted (so the .cs file is only written when non-empty).
        bool WriteCSharpContent(FCodeWriter& Writer, FReflectedHeader* Header);

        void WriteUnityBuildFile(FReflectedProject* Project, const eastl::string& Contents);

    private:

        FReflectedWorkspace*       Workspace = nullptr;
        const FReflectionDatabase* ReflectionDatabase = nullptr;
    };
}
