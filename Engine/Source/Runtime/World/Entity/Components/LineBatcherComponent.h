#pragma once
#include "Containers/Array.h"
#include "Renderer/Vertex.h"

namespace Lumina
{
    struct RUNTIME_API FLineBatcherComponent
    {
        static constexpr auto in_place_delete = true;

        // Self-contained line record. Storing endpoints + packed color
        // directly removes the parallel Vertices array we used to keep,
        // which had to be index-patched on every compaction.
        struct FLineInstance
        {
            glm::vec3   Start;
            glm::vec3   End;
            uint32      ColorPacked;
            float       RemainingLifetime;
            float       Thickness;
            uint8       bDepthTest:1;
            uint8       bSingleFrame:1;
        };

        // Cross-thread record. Same shape as FLineInstance minus the
        // lifetime bookkeeping that DrainQueue fills in.
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

        // Pull every queued line into Lines. Single-threaded; call before
        // reading Lines for render extraction.
        void DrainQueue()
        {
            FQueuedLine Q;
            while (Queue.try_dequeue(Q))
            {
                Lines.emplace_back(FLineInstance
                {
                    .Start              = Q.Start,
                    .End                = Q.End,
                    .ColorPacked        = Q.ColorPacked,
                    .RemainingLifetime  = Q.Duration,
                    .Thickness          = Q.Thickness,
                    .bDepthTest         = Q.bDepthTest,
                    .bSingleFrame       = Q.bSingleFrame,
                });
            }
        }
    };
}
