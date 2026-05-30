local LuminaDir = os.getenv("LUMINA_DIR")
if not LuminaDir then
    error("LUMINA_DIR environment variable is not set. Please run the engine's Setup.bat first.")
end

include (path.join(LuminaDir, "BuildScripts/Dependencies"))
include (path.join(LuminaDir, "BuildScripts/Actions/Reflection"))
include (path.join(LuminaDir, "BuildScripts/GameProject"))

LuminaGameProject({
    Name = "$PROJECTNAME",

    -- Third-party libs this project links directly; must stay link-compatible with the Runtime DLL build.
    Dependencies =
    {
        "ImGui",
        "RPMalloc",
        "EA",
        "EnkiTS",
        "Tracy",
        "Luau",
    },
})
