--[[
    Lumina Engine Setup Action
    Replaces the old Python setup pipeline. Invoked as: premake5 setup [--force]

    Responsibilities:
        1. Download the External dependency archive from a remote URL
        2. Extract it into the repository root
        3. Configure git hooks
        4. Persist LUMINA_DIR for tooling that runs outside the build (Reflector, etc.)

    The orchestrating Setup.bat runs this action and then `premake5 vs2022`.
--]]

local LuminaDir = os.getenv("LUMINA_DIR") or _MAIN_SCRIPT_DIR
include (path.join(LuminaDir, "BuildScripts/Logger"))


-- ============================================================================
-- Configuration
-- ============================================================================

local DEPENDENCY_URL =
    "https://www.dropbox.com/scl/fi/mzad6ruqibzsmam30npju/External.zip?rlkey=egj0adfoytpjydnhbs53qd3lh&st=pw81jqsw&dl=0"

local DEPENDENCY_FILENAME   = "External.zip"
local DEPENDENCY_MARKER_DIR = "External"  -- skip download if this exists
local GIT_HOOKS_PATH        = "BuildScripts/Hooks"


-- ============================================================================
-- Options
-- ============================================================================

newoption 
{
    trigger     = "force",
    description = "Force re-download even if External/ already exists",
}


-- ============================================================================
-- Helpers
-- ============================================================================

local function NormalizeDropboxUrl(url)
    if url:find("dropbox%.com") and url:find("dl=0") then
        return (url:gsub("dl=0", "dl=1"))
    end
    return url
end

local function FormatBytes(bytes)
    if bytes >= 1024 * 1024 then
        return string.format("%.2f MB", bytes / (1024 * 1024))
    elseif bytes >= 1024 then
        return string.format("%.2f KB", bytes / 1024)
    end
    return tostring(bytes) .. " B"
end

local function DownloadArchive(url, dest)
    Logger.Info("Downloading from " .. url)
    Logger.Info("    -> " .. dest)

    local lastPercent = -1
    local result, err = http.download(url, dest, {
        progress = function(total, current)
            if total and total > 0 then
                local percent = math.floor((current / total) * 100)
                if percent ~= lastPercent then
                    lastPercent = percent
                    local bars = math.floor(percent / 2)
                    io.write(string.format(
                        "\r  [%s%s] %3d%%  (%s / %s)",
                        string.rep("#", bars),
                        string.rep("-", 50 - bars),
                        percent,
                        FormatBytes(current),
                        FormatBytes(total)))
                    io.flush()
                end
            end
        end,
    })
    io.write("\n")

    if result ~= "OK" then
        Logger.Error("Download failed: " .. tostring(result) .. " " .. tostring(err or ""))
        return false
    end

    Logger.Success("Download complete.")
    return true
end

local function ExtractArchive(archive, destDir)
    local ext = path.getextension(archive):lower()
    Logger.Info("Extracting " .. archive .. " -> " .. destDir)

    if ext == ".zip" then
        local ok, err = pcall(zip.extract, archive, destDir)
        if not ok then
            Logger.Error("Extraction failed: " .. tostring(err))
            return false
        end
        return true
    end

    if ext == ".7z" then
        local sevenZip = path.join(LuminaDir, "Tools/7zr.exe")
        if not os.isfile(sevenZip) then
            Logger.Error("Cannot extract .7z: " .. sevenZip .. " not found.")
            Logger.Info("Resolve by either:")
            Logger.Info("  (a) Drop 7zr.exe (https://www.7-zip.org/download.html) into Tools/")
            Logger.Info("  (b) Repackage the archive as .zip and update DEPENDENCY_URL")
            return false
        end
        local cmd = string.format('"%s" x -y "%s" -o"%s"',
            path.translate(sevenZip, "\\"),
            path.translate(archive, "\\"),
            path.translate(destDir, "\\"))
        local code = os.execute(cmd)
        if code == 0 or code == true then
            return true
        end
        Logger.Error("7zr.exe exited with status " .. tostring(code))
        return false
    end

    Logger.Error("Unsupported archive extension: " .. ext)
    return false
end

local function PersistEnvVar(name, value)
    if os.host() ~= "windows" then
        Logger.Info(string.format("Skipping %s persistence on non-Windows host.", name))
        return true
    end
    -- setx persists for future shells; failures (e.g. >1024 char value) are non-fatal.
    local cmd = string.format('setx %s "%s" >nul 2>&1', name, value)
    local ok = os.execute(cmd)
    if ok == 0 or ok == true then
        Logger.Success(string.format("%s -> %s (persisted via setx).", name, value))
        return true
    end
    Logger.Warning(string.format(
        "Could not persist %s via setx (this session only).", name))
    return false
end

local function ConfigureGitHooks()
    if not os.isdir(path.join(LuminaDir, ".git")) then
        Logger.Warning("Not a git repository; skipping hooks configuration.")
        return true
    end
    if not os.isdir(path.join(LuminaDir, GIT_HOOKS_PATH)) then
        Logger.Warning(GIT_HOOKS_PATH .. " not found; skipping hooks configuration.")
        return true
    end
    local cmd = string.format('git config core.hooksPath "%s"', GIT_HOOKS_PATH)
    local code = os.execute(cmd)
    if code == 0 or code == true then
        Logger.Success("Git hooks path -> " .. GIT_HOOKS_PATH)
        return true
    end
    Logger.Warning("Failed to configure git hooks path (non-fatal).")
    return false
end


-- ============================================================================
-- Action
-- ============================================================================

newaction {
    trigger     = "setup",
    description = "First-time setup: download External dependencies, configure environment and hooks.",

    execute = function()
        Logger.Info("==========================================================")
        Logger.Info("                 LUMINA ENGINE SETUP                      ")
        Logger.Info("==========================================================")
        Logger.Info("Working directory: " .. LuminaDir)

        -- 1. Persist LUMINA_DIR
        Logger.Info("")
        Logger.Info("[1/4] Configuring environment")
        PersistEnvVar("LUMINA_DIR", LuminaDir)

        -- 2. Dependencies
        Logger.Info("")
        Logger.Info("[2/4] External dependencies")
        local externalDir = path.join(LuminaDir, DEPENDENCY_MARKER_DIR)
        local archivePath = path.join(LuminaDir, DEPENDENCY_FILENAME)
        local skip        = os.isdir(externalDir) and not _OPTIONS["force"]

        if skip then
            Logger.Success("External/ already present; skipping download. Use --force to refresh.")
        else
            local url = NormalizeDropboxUrl(DEPENDENCY_URL)
            if not DownloadArchive(url, archivePath) then
                os.exit(1)
            end
            if not ExtractArchive(archivePath, LuminaDir) then
                Logger.Error("Setup aborted; leaving " .. DEPENDENCY_FILENAME .. " in place for inspection.")
                os.exit(1)
            end
            os.remove(archivePath)
            Logger.Success("Dependencies installed.")
        end

        -- 3. Git hooks
        Logger.Info("")
        Logger.Info("[3/4] Git hooks")
        ConfigureGitHooks()

        -- 4. Done
        Logger.Info("")
        Logger.Info("[4/4] Done")
        Logger.Success("Setup complete.")
        Logger.Info("Next: project files will be generated automatically.")
    end,
}
