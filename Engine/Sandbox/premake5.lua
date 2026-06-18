local LuminaDir = os.getenv("LUMINA_DIR")
if not LuminaDir then
    error("LUMINA_DIR environment variable is not set. Please run the engine's Setup.bat first.")
end

include (path.join(LuminaDir, "BuildScripts/Dependencies"))
include (path.join(LuminaDir, "BuildScripts/Actions/Reflection"))
include (path.join(LuminaDir, "BuildScripts/GameProject"))

LuminaGameProject({
    Name = "Sandbox",

    -- Third-party libs this C++ module links directly. Omitted here so the project tracks the engine's
    -- default game set (ImGui, RPMalloc, EA, Tracy), which stays link-compatible with the Runtime DLL.
    -- Uncomment and extend to link more of the engine's third-party (e.g. "Jolt") into your module.
    -- Dependencies = { "ImGui", "RPMalloc", "EA", "Tracy" },
})
