#pragma once

// Yes, we do define these types just to remove the "_t" from stdint.h... get over it.

//--------------------------------- Unsigned base types

// 8-bit unsigned integer
using uint8  = unsigned char;

// 16-bit unsigned integer
using uint16 = unsigned short int;

// 32-bit unsigned integer
using uint32 = unsigned int;

// 64-bit unsigned integer
using uint64 = unsigned long long;

// 64-bit pointer.
using UINTPTR = uint64;

// Windows SDK `SIZE_T` (ULONG_PTR on x64). Some allocator/memory code uses
// SIZE_T directly and historically picked it up via transitive Windows.h
// pulls from third-party PCH entries; declare it here so the engine doesn't
// rely on that accident.
using SIZE_T = uint64;

//--------------------------------- Signed base types

// 8-bit signed integer
using int8   = signed char;

// 16-bit signed integer
using int16  = signed short int;

// 32-bit signed integer
using int32  = signed int;

// 64-bit signed integer
using int64  = signed long long;

//--------------------------------- Character types

// An ANSI character. 8-bit fixed-width representation of 7-bit characters.
using ANSICHAR = char;

// A wide character. In-memory only. ?-bit fixed-width representation of the platform's natural wide character set.
using WIDECHAR = wchar_t;

// A switchable character. In-memory only. Either ANSICHAR or WIDECHAR.
using TCHAR = WIDECHAR;
