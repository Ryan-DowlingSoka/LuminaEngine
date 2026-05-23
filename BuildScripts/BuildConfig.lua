--[[
    Lumina Build Configuration

    The single file you edit to control which optional engine features get
    compiled into a build. These choices are baked in when you (re)generate the
    Visual Studio solution (Setup.bat / GenerateProjectFiles.bat), so regenerate
    after changing anything here.

    Each feature takes one of three values:

        "auto"  -- engine decides per-configuration (the sensible default).
        "on"    -- force the feature into EVERY configuration (Debug,
                   Development AND Shipping).
        "off"   -- strip the feature out of EVERY configuration. Code and
                   libraries for it are not compiled or linked at all.

    You can also override any single feature for a one-off generation without
    editing this file, by passing a flag to premake. The flag wins over the
    value here:

        Tools\premake5.exe vs2022 --tracy=off --validation=on
        GenerateProjectFiles.bat               (uses the values below)

    "auto" defaults, per configuration:

        Feature      Debug   Development   Shipping    Notes
        ---------    -----   -----------   --------    ---------------------------
        Tracy         on        on           off       CPU/GPU profiler (Tracy).
        Validation    on        off          off       Vulkan validation layers.
        Aftermath    Nvidia*   Nvidia*        off       NVIDIA GPU crash dumps.

        * Aftermath "auto" only turns on when an NVIDIA GPU is detected on the
          machine generating the solution. Force "on" to compile it regardless
          (e.g. building a package on a non-Nvidia CI box for Nvidia hardware).
]]

return {
    -- Tracy profiler (CPU zones, GPU timing, frame markers). When "off", the
    -- Tracy library is dropped from the build entirely and all LUMINA_PROFILE_*
    -- macros expand to nothing.
    Tracy      = "auto",

    -- Vulkan validation layers + synchronization validation. Heavy; meant for
    -- catching API/sync errors during development. Independent of Tracy.
    Validation = "auto",

    -- NVIDIA Nsight Aftermath GPU crash-dump capture. Nvidia-only. "auto"
    -- enables it on Nvidia machines for Debug/Development builds.
    Aftermath  = "auto",

    -- Verbose logging: LOG_TRACE / LOG_DEBUG / LOG_INFO. When inactive, those
    -- macros compile to nothing (format strings dropped, no per-call disk I/O).
    -- LOG_WARN / LOG_ERROR / LOG_CRITICAL are always kept for crash diagnostics.
    -- "auto" keeps verbose logging in Debug/Development and strips it in Shipping.
    VerboseLogging = "auto",
}
