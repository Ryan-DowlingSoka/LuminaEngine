#pragma once
#include "Containers/Array.h"
#include "Renderer/Vertex.h"

namespace Lumina
{
    struct RUNTIME_API FLineBatcherComponent
    {
        static constexpr auto in_place_delete = true;

        struct FLineInstance
        {
            uint32  StartVertexIndex;
            float   RemainingLifetime;
            float   Thickness;
            uint8   bDepthTest:1;
            uint8   bSingleFrame:1;
        };

        // Self-contained line record used to cross the thread boundary. The
        // existing Vertices/Lines layout is index-keyed and only safe to
        // mutate from one thread, so workers push these into Queue and a
        // single drain on the render-extraction tick converts them.
        struct FQueuedLine
        {
            glm::vec3   Start;
            glm::vec3   End;
            uint32      ColorPacked;
            float       Duration;
            float       Thickness;
            uint8       bDepthTest:1;
            uint8       bSingleFrame:1;
        };

        TVector<FSimpleElementVertex>   Vertices;
        TVector<FLineInstance>          Lines;

        // MPMC queue. moodycamel's ConcurrentQueue is lock-free for both
        // enqueue and dequeue; per-call cost is dominated by one atomic
        // CAS on the producer block, which is well under the cost of the
        // vector reserve/emplace path that DrainQueue replaces.
        TConcurrentQueue<FQueuedLine>   Queue;

        // Thread-safe. Call from any worker. The line becomes visible the
        // next time DrainQueue runs (once per render-extraction tick).
        void EnqueueLine(const glm::vec3& Start, const glm::vec3& End, const glm::vec4& Color, float Thickness = 1.0f, bool bDepthTest = true, float Duration = -1.0f)
        {
            FQueuedLine Q;
            Q.Start         = Start;
            Q.End           = End;
            Q.ColorPacked   = PackColor(Color);
            Q.Duration      = Duration;
            Q.Thickness     = Thickness;
            Q.bDepthTest    = bDepthTest ? 1u : 0u;
            Q.bSingleFrame  = (Duration == -1.0f) ? 1u : 0u;
            Queue.enqueue(Q);
        }

        // Pull every queued line into the canonical Vertices/Lines arrays.
        // Single-threaded; call before reading Lines for render extraction.
        void DrainQueue()
        {
            FQueuedLine Q;
            while (Queue.try_dequeue(Q))
            {
                AppendLine(Q.Start, Q.End, Q.ColorPacked, Q.Thickness, Q.bDepthTest != 0, Q.Duration);
            }
        }

        // Single-threaded direct push. Kept for code paths that already
        // know they're on the render thread (and hot enough to skip the
        // queue). New callers should prefer EnqueueLine.
        void DrawLine(const glm::vec3& Start, const glm::vec3& End, const glm::vec4& Color, float Thickness = 1.0f, bool bDepthTest = true, float Duration = -1.0f)
        {
            AppendLine(Start, End, PackColor(Color), Thickness, bDepthTest, Duration);
        }

        void RemoveLine(int32 LineIndex)
        {
            const FLineInstance& Line = Lines[LineIndex];
            uint32 VertexIndex = Line.StartVertexIndex;

            Vertices.erase(Vertices.begin() + VertexIndex, Vertices.begin() + VertexIndex + 2);

            for (int32 i = LineIndex + 1; i < Lines.size(); ++i)
            {
                Lines[i].StartVertexIndex -= 2;
            }

            Lines.erase(Lines.begin() + LineIndex);
        }

    private:

        void AppendLine(const glm::vec3& Start, const glm::vec3& End, uint32 ColorPacked, float Thickness, bool bDepthTest, float Duration)
        {
            if (Vertices.capacity() < Vertices.size() + 2)
            {
                Vertices.reserve(Vertices.capacity() * 2);
                Lines.reserve(Lines.capacity() * 2);
            }

            uint32 StartVertexIndex = static_cast<uint32>(Vertices.size());

            Vertices.emplace_back(FSimpleElementVertex
            {
                .Position = Start,
                .Color    = ColorPacked,
            });

            Vertices.emplace_back(FSimpleElementVertex
            {
                .Position = End,
                .Color    = ColorPacked,
            });

            Lines.emplace_back(FLineInstance
            {
                .StartVertexIndex   = StartVertexIndex,
                .RemainingLifetime  = Duration,
                .Thickness          = Thickness,
                .bDepthTest         = bDepthTest,
                .bSingleFrame       = Duration == -1.0f
            });
        }
    };
}
