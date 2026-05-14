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
		-- slang-glslang hosts the spirv-opt downstream pass; without it slang
		-- emits the "failed to load downstream compiler 'spirv-opt'" warning
		-- and ships unoptimized SPIR-V.
		LuminaConfig.CopyFileIgnoreErrors(LuminaConfig.EnginePath("External/SLang/bin/slang-glslang.dll"), LuminaConfig.GetTargetDirectory()),
		LuminaConfig.CopyFileIgnoreErrors(LuminaConfig.EnginePath("External/SLang/bin/slang-glsl-module.dll"), LuminaConfig.GetTargetDirectory()),
		LuminaConfig.CopyFileIgnoreErrors(LuminaConfig.EnginePath("External/SLang/bin/slang-llvm.dll"), LuminaConfig.GetTargetDirectory()),
		LuminaConfig.CopyFileIgnoreErrors(LuminaConfig.EnginePath("External/SLang/bin/slang-rt.dll"), LuminaConfig.GetTargetDirectory()),
	}