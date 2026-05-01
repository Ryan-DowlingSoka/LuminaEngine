#pragma once
#include "Core/Object/ObjectMacros.h"
#include "glm/glm.hpp"
#include "MaterialTypes.generated.h"

#define MAX_VECTORS 24
#define MAX_SCALARS 24
#define MAX_TEXTURES 24


namespace Lumina
{
    /** GPU material flags - must match EMaterialFlags in Common.slang */
    enum class EMaterialGPUFlags : uint32
    {
        None        = 0,
        Masked      = 1 << 0,
        Translucent = 1 << 1,
        Additive    = 1 << 2,
        Unlit       = 1 << 3,
    };

    ENUM_CLASS_FLAGS(EMaterialGPUFlags);

    struct FMaterialUniforms
    {
        glm::vec4   Vectors[MAX_VECTORS];
        float       Scalars[MAX_SCALARS];
        uint32      Textures[MAX_TEXTURES];
        uint32      Flags;
        float       OpacityClipValue;
        uint32      Padding[2];
    };
    

    REFLECT()
    enum class EMaterialParameterType : uint8
    {
        Scalar,
        Vector,
        Texture,
    };


    REFLECT()
    struct RUNTIME_API FMaterialParameter
    {
        GENERATED_BODY()

        /** Name key used to look up this parameter in the material. */
        PROPERTY()
        FName ParameterName;

        /** Data type of the parameter (Scalar, Vector, or Texture). */
        PROPERTY()
        EMaterialParameterType Type;

        /** Slot index within the appropriate uniform array for this parameter type. */
        PROPERTY()
        uint16 Index;

        /** Authored default for Scalar parameters. PostLoad replays this into
         *  MaterialUniforms because the uniform block itself isn't serialized. */
        PROPERTY()
        float ScalarDefault = 0.0f;

        /** Authored default for Vector parameters. PostLoad replays this into
         *  MaterialUniforms because the uniform block itself isn't serialized. */
        PROPERTY()
        glm::vec4 VectorDefault = glm::vec4(0.0f);
    };
}
