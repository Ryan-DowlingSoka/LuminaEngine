#pragma once

#include "Tools/Import/ImportHelpers.h"

// Editor-only model-format parsers (OBJ/FBX/GLTF) pulling tinyobjloader/OpenFBX/fastgltf, none of which ship in the Game runtime.
// FMeshImportData + the heavy finalize/meshlet passes stay in Runtime (ImportHelpers.h) so cooked meshes finalize without these parsers.
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
