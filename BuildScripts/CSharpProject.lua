-- Premake extension: SDK-style C# projects with the bits premake's stock C# generator can't express.
-- premake (5.0-dev) emits SDK-style csproj + PackageReference, but has no API for Roslyn
-- analyzer/source-generator references, arbitrary SDK <PropertyGroup> properties, or custom <Target>s,
-- and it dumps the C++ workspace defines into every C# project (invalid as C# DefineConstants).
--
-- We capture premake's generated csproj and post-process the XML: inject the raw blocks the project
-- declares, and (optionally) blank the inherited C++ DefineConstants. String post-processing keeps us
-- off premake's undocumented internal call-array, which differs across versions.

local p = premake

-- Project-scope fields; values are raw XML lines emitted verbatim.
p.api.register { name = "dotnetrawprops",    scope = "project", kind = "list:string" }  -- into a trailing <PropertyGroup>
p.api.register { name = "dotnetrawitems",    scope = "project", kind = "list:string" }  -- into a trailing <ItemGroup> (analyzer refs, compile globs)
p.api.register { name = "dotnetrawtail",     scope = "project", kind = "list:string" }  -- raw XML before </Project> (e.g. <Target>)
p.api.register { name = "dotnetstripdefines", scope = "project", kind = "boolean" }     -- blank the inherited C++ defines

require("vstudio")
local dn = p.vstudio.dotnetbase

p.override(dn, "generate", function(base, prj)
    local xml = p.capture(function() base(prj) end)

    -- Blank the C++ workspace DefineConstants (e.g. DLL_EXPORT=__declspec(dllexport)) which are invalid C#.
    if prj.dotnetstripdefines then
        xml = xml:gsub("(<DefineConstants>).-(</DefineConstants>)", "%1%2")
    end

    -- Assemble the raw injection block.
    local lines = {}
    if prj.dotnetrawprops and #prj.dotnetrawprops > 0 then
        lines[#lines+1] = "  <PropertyGroup>"
        for _, l in ipairs(prj.dotnetrawprops) do lines[#lines+1] = "    " .. l end
        lines[#lines+1] = "  </PropertyGroup>"
    end
    if prj.dotnetrawitems and #prj.dotnetrawitems > 0 then
        lines[#lines+1] = "  <ItemGroup>"
        for _, l in ipairs(prj.dotnetrawitems) do lines[#lines+1] = "    " .. l end
        lines[#lines+1] = "  </ItemGroup>"
    end
    if prj.dotnetrawtail then
        for _, l in ipairs(prj.dotnetrawtail) do lines[#lines+1] = "  " .. l end
    end

    if #lines > 0 then
        local block = table.concat(lines, "\n") .. "\n"
        -- function replacement: avoids Lua treating '%'/'$' in the block as gsub specials.
        xml = xml:gsub("</Project>", function() return block .. "</Project>" end, 1)
    end

    p.outln(xml)
end)
