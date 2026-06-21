
WarningLevel = 
{
    Off = "off",
    On = "on",
    Error = "error"
}

LuminaConfig = 
{
    MissingReturnWarningLevel = WarningLevel.Error,

    UnusedVariableWarningLevel = WarningLevel.On,
    UnusedParameterWarningLevel = WarningLevel.Off,
    UnusedFunctionWarningLevel = WarningLevel.On,

    UninitializedWarningLevel = WarningLevel.Error,

    ConversionWarningLevel = WarningLevel.On,
    SignConversionWarningLevel = WarningLevel.Off,

    ShadowWarningLevel = WarningLevel.On,
    DeprecatedWarningLevel = WarningLevel.On,
    FallthroughWarningLevel = WarningLevel.On,

    FormatStringWarningLevel = WarningLevel.Error,
    DivideByZeroWarningLevel = WarningLevel.Error,
}

local WarningMap = 
{
    MissingReturnWarningLevel = {
        msvc = "4035",
        clang = "-Wreturn-type",
        gcc = "-Wreturn-type"
    },
    UnusedVariableWarningLevel = {
        msvc = "4101",
        clang = "-Wunused-variable",
        gcc = "-Wunused-variable"
    },
    UnusedParameterWarningLevel = {
        msvc = "4100",
        clang = "-Wunused-parameter",
        gcc = "-Wunused-parameter"
    },
    UnusedFunctionWarningLevel = {
        msvc = "4505",
        clang = "-Wunused-function",
        gcc = "-Wunused-function"
    },
    UninitializedWarningLevel = {
        msvc = "4700",
        clang = "-Wuninitialized",
        gcc = "-Wuninitialized"
    },
    ConversionWarningLevel = {
        msvc = "4244",
        clang = "-Wconversion",
        gcc = "-Wconversion"
    },
    SignConversionWarningLevel = {
        msvc = "4245",
        clang = "-Wsign-conversion",
        gcc = "-Wsign-conversion"
    },
    ShadowWarningLevel = {
        msvc = "4456",
        clang = "-Wshadow",
        gcc = "-Wshadow"
    },
    DeprecatedWarningLevel = {
        msvc = "4996",
        clang = "-Wdeprecated-declarations",
        gcc = "-Wdeprecated-declarations"
    },
    FormatStringWarningLevel = {
        msvc = "4477",
        clang = "-Wformat",
        gcc = "-Wformat"
    },
    FallthroughWarningLevel = {
        msvc = "4062",
        clang = "-Wimplicit-fallthrough",
        gcc = "-Wimplicit-fallthrough"
    },
    DivideByZeroWarningLevel = {
        msvc = "4723",
        clang = "-Wdivision-by-zero",
        gcc = "-Wdiv-by-zero"
    }
}


