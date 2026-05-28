#pragma once

#ifdef REFLECTION_PARSER

#include "ObjectMacros.h"

namespace Lumina
{
    REFLECT(NoLua)
    struct FVector2
    {
        PROPERTY(Editable)
        float x;

        PROPERTY(Editable)
        float y;
    };

    REFLECT(NoLua)
    struct FVector3
    {
        PROPERTY(Editable)
        float x;

        PROPERTY(Editable)
        float y;

        PROPERTY(Editable)
        float z;
    };

    REFLECT(NoLua)
    struct FVector4
    {
        PROPERTY(Editable)
        float x;

        PROPERTY(Editable)
        float y;

        PROPERTY(Editable)
        float z;

        PROPERTY(Editable)
        float w;
    };

    REFLECT(NoLua)
    struct FQuat
    {
        PROPERTY(Editable)
        float x;

        PROPERTY(Editable)
        float y;

        PROPERTY(Editable)
        float z;

        /** Real component. */
        PROPERTY(Editable)
        float w;
    };
}

#endif