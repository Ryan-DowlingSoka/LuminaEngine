#pragma once

// Shadow sampling textures get their own set (set=3) so shadow-write passes bind SceneBindingSet
// without the cascade/atlas being SRVs there. An image used as both bound SRV and current depth
// attachment trips a WAW layout-transition hazard in the validator; this split avoids it.

[[vk::binding(0, 3)]] Sampler2D uShadowCascade;
[[vk::binding(1, 3)]] Sampler2D uShadowAtlas;
// Same cascade atlas as binding 0, point-sampler for raw depth (PCSS blocker).
[[vk::binding(2, 3)]] Sampler2D uShadowCascadePoint;
