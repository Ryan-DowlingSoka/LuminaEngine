-- LuminaOptions resolves feature toggles (Tracy/Validation/Aftermath/VerboseLogging) from auto-policy, then BuildConfig.lua, then CLI flags (highest priority).

assert(LuminaConfig, "Options.lua must be included after BuildScripts/Dependencies")

LuminaOptions = LuminaOptions or {}

local ALL_CONFIGS = { "Debug", "Development", "Shipping" }


-- NVIDIA GPU detection (Windows) for the Aftermath auto policy; cached on LuminaConfig so per-project queries don't re-shell out.
function LuminaConfig.HasNvidiaGPU()
    if LuminaConfig._HasNvidiaGPU ~= nil then
        return LuminaConfig._HasNvidiaGPU
    end

    local found = false
    if os.host() == "windows" then
        -- Prefer CIM (wmic is deprecated on recent Windows 11); fall back to wmic if PowerShell isn't usable.
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


-- Per-config "auto" policy; functions so Aftermath consults the GPU probe lazily.
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

-- Final mode: CLI flag wins, then BuildConfig.lua, then "auto". FlagTrigger overrides the default lowercased --option name.
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

-- premake filter string for the configs where the feature is active, or nil when active nowhere.
function LuminaOptions.FilterFor(feature)
    local cfgs = LuminaOptions.Configs(feature)
    if #cfgs == 0 then return nil end
    return "configurations:" .. table.concat(cfgs, " or ")
end


-- Wires Aftermath import lib + optional DLL copy onto the current project, scoped to active configs. Call after LuminaModule().
-- DLL copy is POSTBUILD on purpose: prebuild runs before the linker creates the Binaries dir on a clean build, so the copy silently failed.
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


-- Print the resolved feature set once; "auto" shows what it resolved to in parentheses.
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
