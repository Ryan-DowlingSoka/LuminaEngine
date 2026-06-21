local LuminaDir = os.getenv("LUMINA_DIR")
if not LuminaDir then
    error("LUMINA_DIR environment variable is not set. Please run the engine's Setup.bat first.")
end

include (path.join(LuminaDir, "BuildScripts/Dependencies"))
include (path.join(LuminaDir, "BuildScripts/Actions/Reflection"))
include (path.join(LuminaDir, "BuildScripts/GameProject"))

LuminaGameProject({
    Name = "Sandbox",

    -- Omitted so this module tracks the engine default game set, which stays link-compatible with the Runtime DLL.
    -- Dependencies = { "ImGui", "RPMalloc", "EA", "Tracy" },
})
