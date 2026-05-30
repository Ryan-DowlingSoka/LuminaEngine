#pragma once

// Yes, we do define these types just to remove the "_t" from stdint.h... get over it.

using uint8  = unsigned char;
using uint16 = unsigned short int;
using uint32 = unsigned int;
using uint64 = unsigned long long;
using UINTPTR = uint64;
using SIZE_T = uint64; // Windows SDK SIZE_T (ULONG_PTR on x64).

using int8   = signed char;
using int16  = signed short int;
using int32  = signed int;
using int64  = signed long long;

using ANSICHAR = char;
using WIDECHAR = wchar_t;
using TCHAR = WIDECHAR; // Switchable character; either ANSICHAR or WIDECHAR.
