#pragma once
#include "UpdateStage.h"
#include "Platform/GenericPlatform.h"
#include "Subsystems/Subsystem.h"


namespace Lumina
{
    class FSubsystemManager;

    class RUNTIME_API FUpdateContext
    {
    public:

        friend class FEngine;
        
        FORCEINLINE void MarkFrameStart(double InStart)
        {
            if (LastFrameStart > 0.0)
            {
                DeltaTime = SmoothDelta(InStart - LastFrameStart);
            }
            LastFrameStart = InStart;
            FrameStart = InStart;
        }

        FORCEINLINE void MarkFrameEnd(double InTime)
        {
            Frame++;
            Time = InTime;
        }
        
        FORCEINLINE double GetFrameStartTime() const { return FrameStart; }
        FORCEINLINE double GetTime() const { return Time; }
        FORCEINLINE float GetFPS() const { return 1.0f / (float)DeltaTime; }
        FORCEINLINE double GetDeltaTime() const { return DeltaTime; }
        FORCEINLINE uint64 GetFrame() const { return Frame; }
        FORCEINLINE EUpdateStage GetUpdateStage() const { return UpdateStage; }

        
    protected:

        // Running-average filter over recent frame intervals. A single slow frame
        // (GPU-wait spike, scheduler hiccup) is a large fraction of the budget at
        // low FPS; feeding the raw interval straight into the physics accumulator
        // makes the interpolation alpha beat against the frame cadence, which reads
        // as stutter. Averaging removes the high-frequency jitter; the per-frame
        // clamp keeps a pathological hitch (debugger pause, window drag) from
        // poisoning the window or teleporting the simulation.
        static constexpr int32  DeltaSmoothingWindow = 8;
        static constexpr double MaxRawDelta = 1.0 / 10.0;      // 10 FPS floor — sim slows rather than jumps past this
        static constexpr double MinRawDelta = 1.0 / 10000.0;   // guard a zero/negative interval (clock anomaly)

        FORCEINLINE double SmoothDelta(double RawDelta)
        {
            RawDelta = RawDelta < MinRawDelta ? MinRawDelta : (RawDelta > MaxRawDelta ? MaxRawDelta : RawDelta);

            DeltaHistory[DeltaHistoryCursor] = RawDelta;
            DeltaHistoryCursor = (DeltaHistoryCursor + 1) % DeltaSmoothingWindow;
            if (DeltaHistoryCount < DeltaSmoothingWindow)
            {
                ++DeltaHistoryCount;
            }

            double Sum = 0.0;
            for (int32 i = 0; i < DeltaHistoryCount; ++i)
            {
                Sum += DeltaHistory[i];
            }
            return Sum / (double)DeltaHistoryCount;
        }

        double              Time = 0;
        double              FrameStart = 0;
        double              DeltaTime = 1.0 / 60.0;
        double              LastFrameStart = 0.0;
        float               FrameRateLimit = 144.0f;
        uint64              Frame = 0;
        EUpdateStage        UpdateStage = EUpdateStage::Max;

        double              DeltaHistory[DeltaSmoothingWindow] = {};
        int32               DeltaHistoryCount = 0;
        int32               DeltaHistoryCursor = 0;
    };
}
