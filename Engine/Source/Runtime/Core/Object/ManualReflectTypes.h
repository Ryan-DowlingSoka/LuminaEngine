#pragma once

#ifdef REFLECTION_PARSER

#include "ObjectMacros.h"

namespace glm
{
    REFLECT(NoLua)
    struct vec2
    {
        PROPERTY(Editable)
        float x;

        PROPERTY(Editable)
        float y;
    };

    REFLECT(NoLua)
    struct vec3
    {
        PROPERTY(Editable)
        float x;

        PROPERTY(Editable)
        float y;

        PROPERTY(Editable)
        float z;
    };

    REFLECT(NoLua)
    struct vec4
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
    struct quat
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