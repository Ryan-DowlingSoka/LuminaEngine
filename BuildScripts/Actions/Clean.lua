local LuminaDir = os.getenv("LUMINA_DIR")

include (path.join(LuminaDir, "BuildScripts/Logger"))

local p = premake

-- Premake's built-in `clean` only removes the generated project/solution
-- files and the declared target/obj dirs. It leaves the rest of
-- Intermediates/ behind - stale ShaderCache (.lsc) and Reflection (.gen.h)
-- output is the usual culprit for "clean build still broken" reports.
-- Wrap onWorkspace so a full clean purges those trees too.
local CleanAction = p.action.get("clean")
if CleanAction then
    local BaseOnWorkspace = CleanAction.onWorkspace

    CleanAction.onWorkspace = function(wks)
        if BaseOnWorkspace then
            BaseOnWorkspace(wks)
        end

        local Root = wks.location or _MAIN_SCRIPT_DIR

        -- .vs holds VS's own IntelliSense/build cache; stale entries here
        -- survive a project regen and produce phantom errors.
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
    end
end
