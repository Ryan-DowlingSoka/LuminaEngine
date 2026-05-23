--[[
    Lumina Build Options

    Resolves the optional-feature toggles (Tracy, Vulkan validation, NVIDIA
    Aftermath) from three layered sources, lowest priority first:

        1. The "auto" per-configuration policy baked in below.
        2. BuildScripts/BuildConfig.lua  (the user-editable defaults file).
        3. premake command-line flags     (--tracy=, --validation=, --aftermath=).

    The result is LuminaOptions, a small API the rest of the build system queries
    instead of hardcoding feature defines:

        LuminaOptions.IsActive("Tracy", "Shipping")  -> bool (single config)
        LuminaOptions.IsActiveAny("Tracy")           -> bool (any config)
        LuminaOptions.Configs("Tracy")               -> { "Debug", "Development" }
        LuminaOptions.FilterFor("Tracy")             -> "configurations:Debug or Development" | nil
        LuminaOptions.LinkAftermath({ Copy = true }) -> wires links/libdirs/copy on the current project

    Loaded by BuildScripts/Dependencies so it is available to both the engine
    workspace and external game workspaces.
]]

assert(LuminaConfig, "Options.lua must be included after BuildScripts/Dependencies")

LuminaOptions = LuminaOptions or {}

local ALL_CONFIGS = { "Debug", "Development", "Shipping" }


-- ============================================================================
-- NVIDIA GPU detection (Windows only)
--
-- Used by the Aftermath "auto" policy: Aftermath is an NVIDIA-only tool, so
-- there is no point compiling it on AMD/Intel machines. Cached on LuminaConfig
-- so repeated queries (one per consuming project) don't re-shell out.
-- ============================================================================
function LuminaConfig.HasNvidiaGPU()
    if LuminaConfig._HasNvidiaGPU ~= nil then
        return LuminaConfig._HasNvidiaGPU
    end

    local found = false
    if os.host() == "windows" then
        -- Prefer CIM (wmic is deprecated/removed on recent Windows 11). Fall
        -- back to wmic if PowerShell isn't usable for some reason.
        local out = os.outputof('powershell -NoProfile -Command "(Get-CimInstance Win32_VideoController).Name -join \',\'"')
        if not out or out == "" then
            out = os.outputof("wmic path win32_VideoController get name")
        end
        if out and out:lower():find("nvidia", 1, true) then
            found = true
        end
    end

    LuminaConfig._HasNvidiaGPU = found
    return found
end


-- ============================================================================
-- Command-line flags
-- ============================================================================
local MODE_VALUES =
{
    { "auto", "Engine decides per-configuration (default)" },
    { "on",   "Force enabled in every configuration" },
    { "off",  "Force disabled in every configuration" },
}

newoption { trigger = "tracy",      value = "MODE", allowed = MODE_VALUES,
            description = "Tracy profiler: auto|on|off (overrides BuildConfig.lua)" }
newoption { trigger = "validation", value = "MODE", allowed = MODE_VALUES,
            description = "Vulkan validation layers: auto|on|off" }
newoption { trigger = "aftermath",  value = "MODE", allowed = MODE_VALUES,
            description = "NVIDIA Aftermath crash dumps: auto|on|off" }
newoption { trigger = "verbose-logging", value = "MODE", allowed = MODE_VALUES,
            description = "Verbose logging (TRACE/DEBUG/INFO): auto|on|off" }


-- ============================================================================
-- Resolution
-- ============================================================================

-- Per-configuration "auto" policy. A function so Aftermath can consult the GPU
-- probe lazily (only when something actually resolves the Aftermath feature).
local AUTO_POLICY =
{
    Tracy          = function(cfg) return cfg == "Debug" or cfg == "Development" end,
    Validation     = function(cfg) return cfg == "Debug" end,
    Aftermath      = function(cfg) return (cfg == "Debug" or cfg == "Development") and LuminaConfig.HasNvidiaGPU() end,
    VerboseLogging = function(cfg) return cfg == "Debug" or cfg == "Development" end,
}

-- Load the user defaults file once. dofile returns the table the file returns.
local function LoadUserDefaults()
    local cfgPath = path.join(_SCRIPT_DIR, "BuildConfig.lua")
    if os.isfile(cfgPath) then
        local ok, result = pcall(dofile, cfgPath)
        if ok and type(result) == "table" then
            return result
        end
        premake.warn("BuildConfig.lua failed to load (%s); using auto defaults.", tostring(result))
    end
    return {}
end

local UserDefaults = LoadUserDefaults()

-- Final mode for a feature: CLI flag wins, then BuildConfig.lua, then "auto".
-- FlagTrigger is the premake --option name (defaults to the lowercased feature
-- when the names match; VerboseLogging uses the hyphenated --verbose-logging).
local function ResolveMode(feature, FlagTrigger)
    local flag = _OPTIONS[FlagTrigger or feature:lower()]
    if flag and flag ~= "" then
        return flag
    end
    local user = UserDefaults[feature]
    if user == "auto" or user == "on" or user == "off" then
        return user
    end
    return "auto"
end

LuminaOptions.Tracy          = ResolveMode("Tracy")
LuminaOptions.Validation     = ResolveMode("Validation")
LuminaOptions.Aftermath      = ResolveMode("Aftermath")
LuminaOptions.VerboseLogging = ResolveMode("VerboseLogging", "verbose-logging")


-- ============================================================================
-- Query API
-- ============================================================================

function LuminaOptions.IsActive(feature, cfg)
    local mode = LuminaOptions[feature]
    assert(mode, "LuminaOptions: unknown feature '" .. tostring(feature) .. "'")
    if mode == "off" then return false end
    if mode == "on"  then return true  end
    local policy = AUTO_POLICY[feature]
    return policy ~= nil and policy(cfg) or false
end

function LuminaOptions.Configs(feature)
    local out = {}
    for _, cfg in ipairs(ALL_CONFIGS) do
        if LuminaOptions.IsActive(feature, cfg) then
            table.insert(out, cfg)
        end
    end
    return out
end

function LuminaOptions.IsActiveAny(feature)
    return #LuminaOptions.Configs(feature) > 0
end

-- premake filter string scoping to the configs where the feature is active, or
-- nil when it is active nowhere. (Listing all three configs is equivalent to no
-- filter, which is fine.)
function LuminaOptions.FilterFor(feature)
    local cfgs = LuminaOptions.Configs(feature)
    if #cfgs == 0 then return nil end
    return "configurations:" .. table.concat(cfgs, " or ")
end


-- ============================================================================
-- Aftermath linkage helper
--
-- Wires the import lib, search path and (optionally) the runtime DLL copy onto
-- whatever project is currently open, scoped to the configs where Aftermath is
-- active. Call it right after LuminaModule() in the Runtime (Copy=true) and
-- Editor (Copy=false) modules.
--
-- The DLL copy lives in a POSTBUILD step on purpose: prebuild runs before the
-- linker has created the target Binaries directory on a clean build, so the old
-- prebuild copy silently failed and the DLL had to be moved by hand. Postbuild
-- runs after the module's own .dll lands in the target dir, so the dir is
-- guaranteed to exist.
-- ============================================================================
function LuminaOptions.LinkAftermath(Opts)
    local fstr = LuminaOptions.FilterFor("Aftermath")
    if not fstr then return end

    local AftermathLibDir = LuminaConfig.EnginePath("Engine/Source/ThirdParty/NvidiaAftermath/lib")

    filter(fstr)
        libdirs { AftermathLibDir }
        links { "GFSDK_Aftermath_Lib" }
        if Opts and Opts.Copy then
            postbuildcommands
            {
                LuminaConfig.CopyFileIgnoreErrors(
                    path.join(AftermathLibDir, "GFSDK_Aftermath_Lib.x64.dll"),
                    LuminaConfig.GetTargetDirectory()),
            }
        end
    filter {}
end


-- ============================================================================
-- Generation-time summary
--
-- Print the resolved feature set once so it's obvious what got baked into the
-- solution. "auto" shows what it resolved to in parentheses.
-- ============================================================================
local function DescribeFeature(name)
    local mode = LuminaOptions[name]
    if mode ~= "auto" then
        return mode
    end
    local cfgs = LuminaOptions.Configs(name)
    if #cfgs == 0 then
        return "auto (off)"
    end
    return "auto (" .. table.concat(cfgs, ", ") .. ")"
end

print(string.format(
    "[Lumina] Build features: Tracy=%s  Validation=%s  Aftermath=%s  VerboseLogging=%s%s",
    DescribeFeature("Tracy"),
    DescribeFeature("Validation"),
    DescribeFeature("Aftermath"),
    DescribeFeature("VerboseLogging"),
    LuminaConfig.HasNvidiaGPU() and "  [NVIDIA GPU]" or ""))
