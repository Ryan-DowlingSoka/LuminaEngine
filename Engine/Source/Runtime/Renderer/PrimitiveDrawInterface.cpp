#include "PCH.h"
#include "PrimitiveDrawInterface.h"

#include <glm/gtx/quaternion.hpp>

#include "ViewVolume.h"
#include "Containers/Array.h"

namespace Lumina
{
    void IPrimitiveDrawInterface::DrawBox(const glm::vec3& Center, const glm::vec3& HalfExtents,
        const glm::quat& Rotation, const glm::vec4& Color, float Thickness, bool bDepthTest, float Duration)
    {
        glm::vec3 LocalCorners[8] =
        {
            {-HalfExtents.x, -HalfExtents.y, -HalfExtents.z},
            { HalfExtents.x, -HalfExtents.y, -HalfExtents.z},
            { HalfExtents.x,  HalfExtents.y, -HalfExtents.z},
            {-HalfExtents.x,  HalfExtents.y, -HalfExtents.z},

            {-HalfExtents.x, -HalfExtents.y,  HalfExtents.z},
            { HalfExtents.x, -HalfExtents.y,  HalfExtents.z},
            { HalfExtents.x,  HalfExtents.y,  HalfExtents.z},
            {-HalfExtents.x,  HalfExtents.y,  HalfExtents.z},
        };

        glm::vec3 corners[8];
        for (int i = 0; i < 8; ++i)
        {
            corners[i] = Center + glm::rotate(Rotation, LocalCorners[i]);
        }

        DrawLine(corners[0], corners[1], Color, Thickness, bDepthTest, Duration);
        DrawLine(corners[1], corners[2], Color, Thickness, bDepthTest, Duration);
        DrawLine(corners[2], corners[3], Color, Thickness, bDepthTest, Duration);
        DrawLine(corners[3], corners[0], Color, Thickness, bDepthTest, Duration);

        DrawLine(corners[4], corners[5], Color, Thickness, bDepthTest, Duration);
        DrawLine(corners[5], corners[6], Color, Thickness, bDepthTest, Duration);
        DrawLine(corners[6], corners[7], Color, Thickness, bDepthTest, Duration);
        DrawLine(corners[7], corners[4], Color, Thickness, bDepthTest, Duration);

        DrawLine(corners[0], corners[4], Color, Thickness, bDepthTest, Duration);
        DrawLine(corners[1], corners[5], Color, Thickness, bDepthTest, Duration);
        DrawLine(corners[2], corners[6], Color, Thickness, bDepthTest, Duration);
        DrawLine(corners[3], corners[7], Color, Thickness, bDepthTest, Duration);
    }

    void IPrimitiveDrawInterface::DrawSphere(const glm::vec3& Center, float Radius, const glm::vec4& Color,
        uint8 Segments, float Thickness, bool bDepthTest, float Duration)
    {
        for (uint8 lat = 1; lat < Segments; ++lat)
        {
            float latAngle = glm::pi<float>() * lat / Segments;
            float y = Radius * cos(latAngle);
            float ringRadius = Radius * sin(latAngle);

            for (int lon = 0; lon < Segments; ++lon)
            {
                float lonAngle1 = glm::two_pi<float>() * lon / Segments;
                float lonAngle2 = glm::two_pi<float>() * (lon + 1) / Segments;

                glm::vec3 p1 = Center + glm::vec3(ringRadius * cos(lonAngle1), y, ringRadius * sin(lonAngle1));
                glm::vec3 p2 = Center + glm::vec3(ringRadius * cos(lonAngle2), y, ringRadius * sin(lonAngle2));

                DrawLine(p1, p2, Color, Thickness, bDepthTest, Duration);
            }
        }

        for (uint8 lon = 0; lon < Segments; ++lon)
        {
            float lonAngle = glm::two_pi<float>() * lon / Segments;

            for (int lat = 0; lat < Segments; ++lat)
            {
                float latAngle1 = glm::pi<float>() * lat / Segments;
                float latAngle2 = glm::pi<float>() * (lat + 1) / Segments;

                glm::vec3 p1 = Center + glm::vec3(Radius * sin(latAngle1) * cos(lonAngle),
                                                  Radius * cos(latAngle1),
                                                  Radius * sin(latAngle1) * sin(lonAngle));

                glm::vec3 p2 = Center + glm::vec3(Radius * sin(latAngle2) * cos(lonAngle),
                                                  Radius * cos(latAngle2),
                                                  Radius * sin(latAngle2) * sin(lonAngle));

                DrawLine(p1, p2, Color, Thickness, bDepthTest, Duration);
            }
        }
    }

    void IPrimitiveDrawInterface::DrawCapsule(const glm::vec3& Start, const glm::vec3& End, float Radius, const glm::vec4& Color, uint8 Segments, float Thickness, bool bDepthTest, float Duration)
    {
        glm::vec3 axis = End - Start;
        float height = glm::length(axis);
        glm::vec3 direction = height > 0.0f ? axis / height : glm::vec3(0, 1, 0);
        
        glm::vec3 up = glm::abs(direction.y) < 0.999f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        glm::vec3 right = glm::normalize(glm::cross(up, direction));
        glm::vec3 forward = glm::cross(direction, right);
        
        for (uint8 i = 0; i <= Segments; ++i)
        {
            float t = static_cast<float>(i) / Segments;
            glm::vec3 center = glm::mix(Start, End, t);
            
            for (uint8 lon = 0; lon < Segments; ++lon)
            {
                float angle1 = glm::two_pi<float>() * lon / Segments;
                float angle2 = glm::two_pi<float>() * (lon + 1) / Segments;
                
                glm::vec3 p1 = center + Radius * (cos(angle1) * right + sin(angle1) * forward);
                glm::vec3 p2 = center + Radius * (cos(angle2) * right + sin(angle2) * forward);
                
                DrawLine(p1, p2, Color, Thickness, bDepthTest, Duration);
            }
        }
        
        for (uint8 lon = 0; lon < Segments; ++lon)
        {
            float angle = glm::two_pi<float>() * lon / Segments;
            glm::vec3 offset = Radius * (cos(angle) * right + sin(angle) * forward);
            
            DrawLine(Start + offset, End + offset, Color, Thickness, bDepthTest, Duration);
        }
        
        for (uint8 lat = 1; lat <= Segments / 2; ++lat)
        {
            float latAngle = glm::half_pi<float>() * lat / (Segments / 2);
            float y = Radius * sin(latAngle);
            float ringRadius = Radius * cos(latAngle);
            
            for (uint8 lon = 0; lon < Segments; ++lon)
            {
                float lonAngle1 = glm::two_pi<float>() * lon / Segments;
                float lonAngle2 = glm::two_pi<float>() * (lon + 1) / Segments;
                
                glm::vec3 offset1 = ringRadius * (cos(lonAngle1) * right + sin(lonAngle1) * forward) + y * direction;
                glm::vec3 offset2 = ringRadius * (cos(lonAngle2) * right + sin(lonAngle2) * forward) + y * direction;
                
                DrawLine(End + offset1, End + offset2, Color, Thickness, bDepthTest, Duration);
            }
        }
        
        for (uint8 lat = 1; lat <= Segments / 2; ++lat)
        {
            float latAngle = glm::half_pi<float>() * lat / (Segments / 2);
            float y = Radius * sin(latAngle);
            float ringRadius = Radius * cos(latAngle);
            
            for (uint8 lon = 0; lon < Segments; ++lon)
            {
                float lonAngle1 = glm::two_pi<float>() * lon / Segments;
                float lonAngle2 = glm::two_pi<float>() * (lon + 1) / Segments;
                
                glm::vec3 offset1 = ringRadius * (cos(lonAngle1) * right + sin(lonAngle1) * forward) - y * direction;
                glm::vec3 offset2 = ringRadius * (cos(lonAngle2) * right + sin(lonAngle2) * forward) - y * direction;
                
                DrawLine(Start + offset1, Start + offset2, Color, Thickness, bDepthTest, Duration);
            }
        }
        
        for (uint8 lon = 0; lon < Segments; ++lon)
        {
            float lonAngle = glm::two_pi<float>() * lon / Segments;
            glm::vec3 radialDir = cos(lonAngle) * right + sin(lonAngle) * forward;
            
            for (uint8 lat = 0; lat < Segments / 2; ++lat)
            {
                float latAngle1 = glm::half_pi<float>() * lat / (Segments / 2);
                float latAngle2 = glm::half_pi<float>() * (lat + 1) / (Segments / 2);
                
                glm::vec3 p1 = End + Radius * (cos(latAngle1) * radialDir + sin(latAngle1) * direction);
                glm::vec3 p2 = End + Radius * (cos(latAngle2) * radialDir + sin(latAngle2) * direction);
                
                DrawLine(p1, p2, Color, Thickness, Duration);
            }
            
            for (uint8 lat = 0; lat < Segments / 2; ++lat)
            {
                float latAngle1 = glm::half_pi<float>() * lat / (Segments / 2);
                float latAngle2 = glm::half_pi<float>() * (lat + 1) / (Segments / 2);
                
                glm::vec3 p1 = Start + Radius * (cos(latAngle1) * radialDir - sin(latAngle1) * direction);
                glm::vec3 p2 = Start + Radius * (cos(latAngle2) * radialDir - sin(latAngle2) * direction);
                
                DrawLine(p1, p2, Color, Thickness, bDepthTest, Duration);
            }
        }
    }

    void IPrimitiveDrawInterface::DrawCone(const glm::vec3& Apex, const glm::vec3& Direction, float AngleRadians,
                                           float Length, const glm::vec4& Color, uint8 Segments, uint8 Stacks, float Thickness, bool bDepthTest, float Duration)
    {
        glm::vec3 dir = glm::normalize(Direction);
        
        glm::vec3 up = glm::abs(dir.y) < 0.99f ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
        glm::vec3 right = glm::normalize(glm::cross(dir, up));
        glm::vec3 coneUp = glm::normalize(glm::cross(right, dir));
        
        for (uint8 stack = 1; stack <= Stacks; ++stack)
        {
            float t = (float)stack / Stacks;
            float ringLength = t * Length;
            float ringRadius = ringLength * tan(AngleRadians);
        
            TVector<glm::vec3> circlePoints(Segments);
            for (int i = 0; i < Segments; ++i)
            {
                float theta = glm::two_pi<float>() * i / Segments;
                circlePoints[i] = Apex + dir * ringLength + ringRadius * (cos(theta) * right + sin(theta) * coneUp);
            }
        
            for (uint8 i = 0; i < Segments; ++i)
            {
                DrawLine(circlePoints[i], circlePoints[(i + 1) % Segments], Color, Thickness, bDepthTest, Duration);
            }
        
            for (uint8 i = 0; i < Segments; ++i)
            {
                DrawLine(Apex, circlePoints[i], Color, Thickness, bDepthTest, Duration);
            }
        
            if (stack > 1)
            {
                float tPrev = (float)(stack - 1) / Stacks;
                float prevLength = tPrev * Length;
                float prevRadius = prevLength * tan(AngleRadians);
        
                for (int i = 0; i < Segments; ++i)
                {
                    glm::vec3 prevPoint = Apex + dir * prevLength + prevRadius * (cos(2*glm::pi<float>() * i / Segments) * right +
                                                                               sin(2*glm::pi<float>() * i / Segments) * coneUp);
                    DrawLine(prevPoint, circlePoints[i], Color, Thickness, bDepthTest, Duration);
                }
            }
        }
    }

    void IPrimitiveDrawInterface::DrawFrustum(const glm::mat4& Matrix, float zNear, float zFar, const glm::vec4& Color, float Thickness, bool bDepthTest, float Duration)
    {
        auto UnprojectCorner = [&](float x, float y, float z) -> glm::vec3
        {
            glm::vec4 ndc(x, y, z, 1.0f);
            glm::vec4 world = glm::inverse(Matrix) * ndc;
            return glm::vec3(world) / world.w;
        };

        glm::vec3 corners[8];

        corners[0] = UnprojectCorner(-1, -1, zNear);
        corners[1] = UnprojectCorner( 1, -1, zNear);
        corners[2] = UnprojectCorner( 1,  1, zNear);
        corners[3] = UnprojectCorner(-1,  1, zNear);

        corners[4] = UnprojectCorner(-1, -1, zFar);
        corners[5] = UnprojectCorner( 1, -1, zFar);
        corners[6] = UnprojectCorner( 1,  1, zFar);
        corners[7] = UnprojectCorner(-1,  1, zFar);


        DrawLine(corners[0], corners[1], Color, Thickness, bDepthTest, Duration);
        DrawLine(corners[1], corners[2], Color, Thickness, bDepthTest, Duration);
        DrawLine(corners[2], corners[3], Color, Thickness, bDepthTest, Duration);
        DrawLine(corners[3], corners[0], Color, Thickness, bDepthTest, Duration);

        DrawLine(corners[4], corners[5], Color, Thickness, bDepthTest, Duration);
        DrawLine(corners[5], corners[6], Color, Thickness, bDepthTest, Duration);
        DrawLine(corners[6], corners[7], Color, Thickness, bDepthTest, Duration);
        DrawLine(corners[7], corners[4], Color, Thickness, bDepthTest, Duration);

        DrawLine(corners[0], corners[4], Color, Thickness, bDepthTest, Duration);
        DrawLine(corners[1], corners[5], Color, Thickness, bDepthTest, Duration);
        DrawLine(corners[2], corners[6], Color, Thickness, bDepthTest, Duration);
        DrawLine(corners[3], corners[7], Color, Thickness, bDepthTest, Duration);
    }

    void IPrimitiveDrawInterface::DrawArrow(const glm::vec3& Start, const glm::vec3& Direction, float Length, const glm::vec4& Color, float Thickness, bool bDepthTest, float Duration, float HeadSize)
    {
        glm::vec3 End = Start + glm::normalize(Direction) * Length;

        DrawLine(Start, End, Color, Thickness, bDepthTest, Duration);

        glm::vec3 Up(0, 1, 0);
        if (glm::abs(glm::dot(glm::normalize(Direction), Up)) > 0.99f)
        {
            Up = glm::vec3(1, 0, 0);
        }
        glm::vec3 Right = glm::normalize(glm::cross(Direction, Up));
        Up = glm::normalize(glm::cross(Right, Direction));

        glm::vec3 Tip = End;
        glm::vec3 BaseCenter = End - glm::normalize(Direction) * HeadSize;

        glm::vec3 Corner1 = BaseCenter + (Up + Right) * HeadSize * 0.5f;
        glm::vec3 Corner2 = BaseCenter + (Up - Right) * HeadSize * 0.5f;
        glm::vec3 Corner3 = BaseCenter + (-Up - Right) * HeadSize * 0.5f;
        glm::vec3 Corner4 = BaseCenter + (-Up + Right) * HeadSize * 0.5f;

        DrawLine(Tip, Corner1, Color, Thickness, bDepthTest, Duration);
        DrawLine(Tip, Corner2, Color, Thickness, bDepthTest, Duration);
        DrawLine(Tip, Corner3, Color, Thickness, bDepthTest, Duration);
        DrawLine(Tip, Corner4, Color, Thickness, bDepthTest, Duration);

        DrawLine(Corner1, Corner2, Color, Thickness, bDepthTest, Duration);
        DrawLine(Corner2, Corner3, Color, Thickness, bDepthTest, Duration);
        DrawLine(Corner3, Corner4, Color, Thickness, bDepthTest, Duration);
        DrawLine(Corner4, Corner1, Color, Thickness, bDepthTest, Duration);
    }

    void IPrimitiveDrawInterface::DrawViewVolume(const FViewVolume& ViewVolume, const glm::vec4& Color, float Thickness, bool bDepthTest, float Duration)
    {
        DrawFrustum(ViewVolume.GetViewProjectionMatrix(), ViewVolume.GetNear(), ViewVolume.GetFar(), Color, Thickness, bDepthTest, Duration);
    }
}

