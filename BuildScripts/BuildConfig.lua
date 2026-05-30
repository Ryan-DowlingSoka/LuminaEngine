-- Optional feature toggles, baked in at solution generation (regenerate after editing). Each is "auto" (engine decides per-config), "on" (every config), or "off" (stripped).
-- Override per-generation via premake flags (e.g. --tracy=off --validation=on), which win over the values here.

return {
    -- Tracy profiler; auto = on in Debug/Development.
    Tracy      = "auto",

    -- Vulkan validation + sync layers; auto = on in Debug only.
    Validation = "auto",

    -- NVIDIA Aftermath GPU crash dumps; auto = on for Debug/Development on Nvidia machines only.
    Aftermath  = "auto",

    -- LOG_TRACE/DEBUG/INFO; auto = kept in Debug/Development, stripped in Shipping. WARN/ERROR/CRITICAL always kept.
    VerboseLogging = "auto",
}
