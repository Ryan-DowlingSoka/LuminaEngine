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

// Encoding-intent aliases. The engine treats narrow char buffers (ANSICHAR/UTF8CHAR) as UTF-8 and
// wide buffers (WIDECHAR/TCHAR) as UTF-16 on Windows. See PlatformString.h / StringCast.
using UTF8CHAR  = char;
using UTF16CHAR = char16_t;
using UTF32CHAR = char32_t;

// Width of a single WIDECHAR in bytes: 2 on Windows (UTF-16), 4 on most Unix platforms (UTF-32).
#define PLATFORM_WIDECHAR_SIZE sizeof(WIDECHAR)
