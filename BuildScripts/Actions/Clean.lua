-- Fall back to _MAIN_SCRIPT_DIR so `premake5 clean` works before Setup.bat sets LUMINA_DIR.
local LuminaDir = os.getenv("LUMINA_DIR") or _MAIN_SCRIPT_DIR

include (path.join(LuminaDir, "BuildScripts/Logger"))

local p = premake

-- Built-in clean leaves Intermediates/ behind; wrap onWorkspace to purge it too.
local CleanAction = p.action.get("clean")
if CleanAction then
    local BaseOnWorkspace = CleanAction.onWorkspace

    CleanAction.onWorkspace = function(wks)
        if BaseOnWorkspace then
            BaseOnWorkspace(wks)
        end

        local Root = wks.location or _MAIN_SCRIPT_DIR

        -- .vs IntelliSense cache survives a regen and produces phantom errors, so purge it too.
        local PurgeDirs =
        {
            path.join(Root, "Intermediates"),
            path.join(Root, ".vs"),
        }

        for _, Dir in ipairs(PurgeDirs) do
            if os.isdir(Dir) then
                Logger.Info("Purging " .. Dir)
                os.rmdir(Dir)
            end
        end

        -- Purge generated .props so a regen re-emits cleanly and stale restore paths don't linger.
        local PropGlobs =
        {
            path.join(Root, "Engine/Source/LuminaSharp*/**.props"),
        }

        for _, Glob in ipairs(PropGlobs) do
            for _, File in ipairs(os.matchfiles(Glob)) do
                Logger.Info("Purging " .. File)
                os.remove(File)
            end
        end
    end
end
