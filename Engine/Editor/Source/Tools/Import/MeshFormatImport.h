#pragma once

#include "Tools/Import/ImportHelpers.h"

// Model-format parsers (OBJ/FBX/GLTF). Editor-only: they pull tinyobjloader,
// OpenFBX and fastgltf, none of which ship in the Game runtime. The parsed
// FMeshImportData and the heavy finalize/meshlet passes still live in Runtime
// (ImportHelpers.h) so cooked meshes can be finalized without these parsers.
namespace Lumina::Import::Mesh
{
    namespace OBJ
    {
        NODISCARD EDITOR_API TExpected<FMeshImportData, FString> ImportOBJ(const FMeshImportOptions& ImportOptions, FStringView FilePath, FScopedSlowTask* Progress = nullptr);
    }

    namespace FBX
    {
        NODISCARD EDITOR_API TExpected<FMeshImportData, FString> ImportFBX(const FMeshImportOptions& ImportOptions, FStringView FilePath, FScopedSlowTask* Progress = nullptr);
    }

    namespace GLTF
    {
        NODISCARD EDITOR_API TExpected<FMeshImportData, FString> ImportGLTF(const FMeshImportOptions& ImportOptions, FStringView FilePath, FScopedSlowTask* Progress = nullptr);
    }
}
