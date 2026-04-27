project "SLang"
	kind "Utility"
	warnings "off"

	files
	{
		"**.h",
		"**.lua",
	}

	postbuildcommands
	{
		LuminaConfig.MakeDirectory(LuminaConfig.GetTargetDirectory()),
		-- Tolerate failure: when the editor itself triggers a Game-platform
		-- rebuild (e.g. via the Project Packager), the same slang DLLs may
		-- already be loaded by the running editor and locked from overwrite.
		-- The DLL on disk is the same one we'd copy, so skipping is safe.
		LuminaConfig.CopyFileIgnoreErrors(LuminaConfig.EnginePath("External/SLang/bin/slang.dll"), LuminaConfig.GetTargetDirectory()),
		LuminaConfig.CopyFileIgnoreErrors(LuminaConfig.EnginePath("External/SLang/bin/slang-compiler.dll"), LuminaConfig.GetTargetDirectory()),
	}