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
		LuminaConfig.CopyFileIgnoreErrors(LuminaConfig.EnginePath("External/SLang/bin/slang.dll"), LuminaConfig.GetTargetDirectory()),
		LuminaConfig.CopyFileIgnoreErrors(LuminaConfig.EnginePath("External/SLang/bin/slang-compiler.dll"), LuminaConfig.GetTargetDirectory()),
		LuminaConfig.CopyFileIgnoreErrors(LuminaConfig.EnginePath("External/SLang/bin/slang-glsl-module.dll"), LuminaConfig.GetTargetDirectory()),
		--LuminaConfig.CopyFileIgnoreErrors(LuminaConfig.EnginePath("External/SLang/bin/slang-glslang.dll"), LuminaConfig.GetTargetDirectory()),
		--LuminaConfig.CopyFileIgnoreErrors(LuminaConfig.EnginePath("External/SLang/bin/slang-llvm.dll"), LuminaConfig.GetTargetDirectory()),
		LuminaConfig.CopyFileIgnoreErrors(LuminaConfig.EnginePath("External/SLang/bin/slang-rt.dll"), LuminaConfig.GetTargetDirectory()),
	}