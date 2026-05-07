#pragma once

// Shadow sampling textures live in their own descriptor set (set=3) so that the
// shadow rendering passes (Point/Spot/Cascaded) can bind SceneBindingSet without
// the cascade/atlas being declared as SRVs there. When the same image is both an
// SRV in a bound set and a depth attachment for the current pass, the state
// tracker emits two layout transitions on the same image in a single barrier
// call which is incoherent and trips SYNC-HAZARD-WRITE-AFTER-WRITE in the
// validator. Splitting this set out keeps the shadow-write passes free of any
// shader-read references to those images.

[[vk::binding(0, 3)]] Sampler2D uShadowCascade;
[[vk::binding(1, 3)]] Sampler2D uShadowAtlas;
// Same cascade atlas as binding 0, point-sampler for raw depth (PCSS blocker).
[[vk::binding(2, 3)]] Sampler2D uShadowCascadePoint;
