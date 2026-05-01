#pragma once

#include "Containers/Array.h"
#include "glm/glm.hpp"
#include "Renderer/Vertex.h"


namespace Lumina::PrimitiveMeshes
{
    inline void GenerateCube(TVector<FVertex>& OutVertices, TVector<uint32>& OutIndices)
    {
        const glm::vec3 normals[] =
        {
            { 0,  0,  1}, { 0,  0, -1}, {-1,  0,  0},
            { 1,  0,  0}, { 0,  1,  0}, { 0, -1,  0}
        };

        const glm::vec3 positions[24] =
        {
            // Front
            {-1, -1,  1}, {1, -1,  1}, {1, 1,  1}, {-1, 1,  1},
            // Back
            {1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1},
            // Left
            {-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1},
            // Right
            {1, -1, 1}, {1, -1, -1}, {1, 1, -1}, {1, 1, 1},
            // Top
            {-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1},
            // Bottom
            {-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1},
        };

        const glm::vec2 uvs[4] =
        {
            {0, 0}, {1, 0}, {1, 1}, {0, 1}
        };

        OutVertices.clear();
        OutIndices.clear();
        OutVertices.reserve(24);

        for (int face = 0; face < 6; ++face)
        {
            for (int i = 0; i < 4; ++i)
            {
                int idx = face * 4 + i;
            
                FVertex vertex;
                vertex.Position = positions[idx];
                vertex.Normal = PackNormal(normals[face]);
                vertex.Tangent = 0; // MikkTSpace fills this in GenerateMeshlets; zero so dedup byte-compare works.
                vertex.UV = glm::packHalf2x16(uvs[i]);
                vertex.Color = 0xFFFFFFFF; // White

                OutVertices.push_back(vertex);
            }

            uint32 base = face * 4;
            OutIndices.push_back(base + 0);
            OutIndices.push_back(base + 1);
            OutIndices.push_back(base + 2);
            OutIndices.push_back(base + 2);
            OutIndices.push_back(base + 3);
            OutIndices.push_back(base + 0);
        }
    }

    inline void GeneratePlane(TVector<FVertex>& OutVertices, TVector<uint32>& OutIndices)
    {
        const glm::vec3 normal = { 0, 0, 1 };
        const glm::vec3 positions[4] =
        {
            {-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}
        };

        const glm::vec2 uvs[4] = { {0,0}, {1,0}, {1,1}, {0,1} };

        OutVertices.clear();
        OutIndices.clear();
        OutVertices.reserve(4);
        OutIndices.reserve(6);

        for (int i = 0; i < 4; ++i)
        {
            FVertex v;
            v.Position = positions[i];
            v.Normal = PackNormal(normal);
            v.Tangent = 0;
            v.UV     = glm::packHalf2x16(uvs[i]);
            v.Color = 0xFFFFFFFF;
            OutVertices.push_back(v);
        }

        OutIndices = { 0, 1, 2, 2, 3, 0 };
    }

    inline void GenerateSphere(TVector<FVertex>& OutVertices, TVector<uint32>& OutIndices, int LatitudeSegments = 16, int LongitudeSegments = 32)
    {
        OutVertices.clear();
        OutIndices.clear();

        for (int y = 0; y <= LatitudeSegments; ++y)
        {
            float v = (float)y / LatitudeSegments;
            float phi = v * glm::pi<float>();

            for (int x = 0; x <= LongitudeSegments; ++x)
            {
                float u = (float)x / LongitudeSegments;
                float theta = u * glm::two_pi<float>();

                glm::vec3 pos =
                {
                    std::sin(phi) * std::cos(theta),
                    std::cos(phi),
                    std::sin(phi) * std::sin(theta)
                };

                FVertex vert;
                vert.Position = pos;
                vert.Normal = PackNormal(glm::normalize(pos));
                vert.Tangent = 0;
                vert.UV     = glm::packHalf2x16(glm::vec2(u, v));
                vert.Color = 0xFFFFFFFF;
                OutVertices.push_back(vert);
            }
        }

        for (int y = 0; y < LatitudeSegments; ++y)
        {
            for (int x = 0; x < LongitudeSegments; ++x)
            {
                uint32 i0 = y * (LongitudeSegments + 1) + x;
                uint32 i1 = i0 + LongitudeSegments + 1;

                OutIndices.push_back(i0);
                OutIndices.push_back(i0 + 1);
                OutIndices.push_back(i1);

                // Second triangle - reversed
                OutIndices.push_back(i0 + 1);
                OutIndices.push_back(i1 + 1);
                OutIndices.push_back(i1);
            }
        }
    }

    inline void GenerateCylinder(TVector<FVertex>& OutVertices, TVector<uint32>& OutIndices, int Segments = 32)
    {
        OutVertices.clear();
        OutIndices.clear();
    
        float halfHeight = 1.0f;
    
        // Side vertices
        for (int i = 0; i <= Segments; ++i)
        {
            float u = (float)i / Segments;
            float theta = u * glm::two_pi<float>();
            glm::vec3 dir = { std::cos(theta), 0, std::sin(theta) };
    
            for (int j = 0; j < 2; ++j)
            {
                FVertex v;
                v.Position = { dir.x, j ? halfHeight : -halfHeight, dir.z };
                v.Normal = PackNormal(glm::normalize(dir));
                v.Tangent = 0;
                v.UV = glm::packHalf2x16(glm::vec2(u, j));
                v.Color = 0xFFFFFFFF;
                OutVertices.push_back(v);
            }
        }
    
        // Side indices
        for (int i = 0; i < Segments; ++i)
        {
            uint32 base = i * 2;
            OutIndices.push_back(base);
            OutIndices.push_back(base + 1);
            OutIndices.push_back(base + 2);
            OutIndices.push_back(base + 2);
            OutIndices.push_back(base + 1);
            OutIndices.push_back(base + 3);
        }
    
        // Top + bottom caps
        uint32 baseIndex = (uint32)OutVertices.size();
    
        for (int cap = 0; cap < 2; ++cap)
        {
            float y = cap ? halfHeight : -halfHeight;
            glm::vec3 n = { 0, cap ? 1 : -1, 0 };
    
            // center vertex
            FVertex center;
            center.Position = { 0, y, 0 };
            center.Normal = PackNormal(n);
            center.Tangent = 0;
            center.UV = glm::packHalf2x16(glm::vec2(32768, 32768));
            center.Color = 0xFFFFFFFF;
            OutVertices.push_back(center);
            uint32 centerIdx = (uint32)OutVertices.size() - 1;

            for (int i = 0; i <= Segments; ++i)
            {
                float u = (float)i / Segments;
                float theta = u * glm::two_pi<float>();
                glm::vec3 dir = { std::cos(theta), 0, std::sin(theta) };

                FVertex v;
                v.Position = { dir.x, y, dir.z };
                v.Normal = PackNormal(n);
                v.Tangent = 0;
                v.UV    = glm::packHalf2x16(glm::vec2(u, cap));
                v.Color = 0xFFFFFFFF;
                OutVertices.push_back(v);
    
                if (i > 0)
                {
                    uint32 a = centerIdx;
                    uint32 b = centerIdx + i;
                    uint32 c = centerIdx + i + 1;
                    if (cap)
                        OutIndices.insert(OutIndices.end(), { a, c, b });
                    else
                        OutIndices.insert(OutIndices.end(), { a, b, c });
                }
            }
        }
    }

    inline void GenerateCone(TVector<FVertex>& OutVertices, TVector<uint32>& OutIndices, int Segments = 32)
    {
        OutVertices.clear();
        OutIndices.clear();

        const float halfHeight = 1.0f;
        const float radius     = 1.0f;
        const float H          = halfHeight * 2.0f;

        // Side faces — duplicate verts per segment so each face gets its own normal
        // (smoothing across the apex looks bad on a cone).
        for (int i = 0; i < Segments; ++i)
        {
            float u0 = (float)i / Segments;
            float u1 = (float)(i + 1) / Segments;
            float theta0 = u0 * glm::two_pi<float>();
            float theta1 = u1 * glm::two_pi<float>();
            float thetaMid = (theta0 + theta1) * 0.5f;

            glm::vec3 normal0   = glm::normalize(glm::vec3(H * std::cos(theta0),   radius, H * std::sin(theta0)));
            glm::vec3 normal1   = glm::normalize(glm::vec3(H * std::cos(theta1),   radius, H * std::sin(theta1)));
            glm::vec3 normalTip = glm::normalize(glm::vec3(H * std::cos(thetaMid), radius, H * std::sin(thetaMid)));

            FVertex v0;
            v0.Position = { std::cos(theta0) * radius, -halfHeight, std::sin(theta0) * radius };
            v0.Normal   = PackNormal(normal0);
            v0.Tangent  = 0;
            v0.UV       = glm::packHalf2x16(glm::vec2(u0, 0));
            v0.Color    = 0xFFFFFFFF;

            FVertex v1;
            v1.Position = { std::cos(theta1) * radius, -halfHeight, std::sin(theta1) * radius };
            v1.Normal   = PackNormal(normal1);
            v1.Tangent  = 0;
            v1.UV       = glm::packHalf2x16(glm::vec2(u1, 0));
            v1.Color    = 0xFFFFFFFF;

            FVertex vTip;
            vTip.Position = { 0, halfHeight, 0 };
            vTip.Normal   = PackNormal(normalTip);
            vTip.Tangent  = 0;
            vTip.UV       = glm::packHalf2x16(glm::vec2((u0 + u1) * 0.5f, 1));
            vTip.Color    = 0xFFFFFFFF;

            uint32 base = (uint32)OutVertices.size();
            OutVertices.push_back(v0);
            OutVertices.push_back(v1);
            OutVertices.push_back(vTip);

            OutIndices.push_back(base + 0);
            OutIndices.push_back(base + 2);
            OutIndices.push_back(base + 1);
        }

        // Bottom cap
        const glm::vec3 capNormal = { 0, -1, 0 };

        FVertex centerVert;
        centerVert.Position = { 0, -halfHeight, 0 };
        centerVert.Normal   = PackNormal(capNormal);
        centerVert.Tangent  = 0;
        centerVert.UV       = glm::packHalf2x16(glm::vec2(0.5f, 0.5f));
        centerVert.Color    = 0xFFFFFFFF;

        uint32 centerIdx = (uint32)OutVertices.size();
        OutVertices.push_back(centerVert);

        for (int i = 0; i <= Segments; ++i)
        {
            float u = (float)i / Segments;
            float theta = u * glm::two_pi<float>();
            glm::vec3 dir = { std::cos(theta), 0, std::sin(theta) };

            FVertex v;
            v.Position = { dir.x * radius, -halfHeight, dir.z * radius };
            v.Normal   = PackNormal(capNormal);
            v.Tangent  = 0;
            v.UV       = glm::packHalf2x16(glm::vec2(0.5f + 0.5f * dir.x, 0.5f + 0.5f * dir.z));
            v.Color    = 0xFFFFFFFF;
            OutVertices.push_back(v);

            if (i > 0)
            {
                uint32 a = centerIdx;
                uint32 b = centerIdx + i;
                uint32 c = centerIdx + i + 1;
                OutIndices.insert(OutIndices.end(), { a, b, c });
            }
        }
    }
}
