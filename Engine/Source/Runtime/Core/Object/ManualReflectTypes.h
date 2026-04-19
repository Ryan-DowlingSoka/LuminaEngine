#pragma once

#ifdef REFLECTION_PARSER

#include "ObjectMacros.h"

namespace glm
{
    REFLECT(NoLua)
    struct vec2
    {
        /** X component. */
        PROPERTY(Editable)
        float x;

        /** Y component. */
        PROPERTY(Editable)
        float y;
    };

    REFLECT(NoLua)
    struct vec3
    {
        /** X component. */
        PROPERTY(Editable)
        float x;

        /** Y component. */
        PROPERTY(Editable)
        float y;

        /** Z component. */
        PROPERTY(Editable)
        float z;
    };

    REFLECT(NoLua)
    struct vec4
    {
        /** X component. */
        PROPERTY(Editable)
        float x;

        /** Y component. */
        PROPERTY(Editable)
        float y;

        /** Z component. */
        PROPERTY(Editable)
        float z;

        /** W component. */
        PROPERTY(Editable)
        float w;
    };

    REFLECT(NoLua)
    struct quat
    {
        /** X (imaginary i) component of the quaternion. */
        PROPERTY(Editable)
        float x;

        /** Y (imaginary j) component of the quaternion. */
        PROPERTY(Editable)
        float y;

        /** Z (imaginary k) component of the quaternion. */
        PROPERTY(Editable)
        float z;

        /** W (real) component of the quaternion. */
        PROPERTY(Editable)
        float w;
    };
}

#endif