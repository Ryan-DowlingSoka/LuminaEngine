
local LuminaDir = os.getenv("LUMINA_DIR")

include (path.join(LuminaDir, "BuildScripts/Logger"))

function Capitalize(str)
    if not str or str == "" then
        return ""
    end
    return str:sub(1, 1):upper() .. str:sub(2)
end

ArchBits =
{
    ["x86"]     = "32",
    ["x86_64"]  = "64",
    ["ARM64"]   = "ARM64"
}

premake.modules.lua = {}
local m = premake.modules.lua

local p = premake

local json = require("json")

local ProjectFiles = {}
local Workspace = {}

newaction 
{
	trigger = "Reflection",
	description = "Builds necessary reflection info and packs into a json file.",

	onStart = function()
        ProjectFiles = {}
	end,

	onWorkspace = function(wks)
        Workspace = wks
	end,

	onProject = function(prj)

        if not prj.enablereflection then
            return
        end

        ProjectFiles[prj.name] = {
            Path = prj.basedir,
            Files = {},
            IncludeDirs = {}
        }
        
        for Config in p.project.eachconfig(prj) do
            for _, IncludePath in ipairs(Config.includedirs) do
                local Path = path.getabsolute(IncludePath)
                
                if not table.contains(ProjectFiles[prj.name].IncludeDirs, Path) then
                    table.insert(ProjectFiles[prj.name].IncludeDirs, Path)
                end
            end
        end

        local Tree = p.project.getsourcetree(prj)
        local function TraverseTree(node)

            if node.abspath then
                local Ext = path.getextension(node.abspath)
                if Ext == ".h" then
                    table.insert(ProjectFiles[prj.name].Files, node.abspath)
                end
            end

            if node.children then
                for _, child in ipairs(node.children) do
                    TraverseTree(child)
                end
            end

        end

        TraverseTree(Tree)
	end,
    
    execute = function()

        local Data = {
            WorkspaceName = Workspace.name,
            WorkspacePath = _MAIN_SCRIPT_DIR,
            Projects = {}
        }
    
        for Name, ProjectData in pairs(ProjectFiles) do
            table.insert(Data.Projects, {
                Name        = Name,
                IncludeDirs = ProjectData.IncludeDirs,
                Files       = ProjectData.Files,
                Path        = ProjectData.Path
            })
        end
    
        local File = io.open("Reflection_Files.json", "w")
        if File then
            File:write(json.encode(Data))
            File:close()
        end

        local SystemName = Capitalize(os.host())

        local Extension = ""
        if SystemName == "Windows" then
            Extension = ".exe"
        end

        local ReflectionDirectory = path.join(os.getenv("LUMINA_DIR"), "Binaries", SystemName .. "64", "Reflector" .. Extension)
        local CmdLine = ReflectionDirectory .. " " .. path.getabsolute("Reflection_Files.json")

        
        if SystemName == "Windows" then
            CmdLine = CmdLine:gsub("/", "\\")
        end
        
        Logger.Info("Executing Command Line " .. CmdLine)
        local Result = os.execute(CmdLine)

        -- Lua's os.execute returns either an integer exit code or a boolean
        -- (older Lua) -- normalise both into a "did this succeed?" check.
        local bOk = (Result == 0) or (Result == true)

        if bOk then
            Logger.Success("Reflection completed successfully!")
            os.remove("Reflection_Files.json")
        else
            -- The Reflector already emitted MSBuild-formatted error lines to
            -- stderr; we just need to fail the action so ReflectionRunner.bat
            -- forwards a non-zero exit to MSBuild and the build halts.
            Logger.Error("Reflection failed - keeping Reflection_Files.json for debugging")
            os.exit(1)
        end
    end,

	onEnd = function()
	end
}

return m