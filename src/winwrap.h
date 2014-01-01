#ifndef WINWRAP_H
#define WINWRAP_H

#include <windows.h>
#include <tchar.h>

// duplicates a string using the LocalAlloc to allocate memory
LPTSTR LocalStrDup(LPCTSTR source);
// duplicates a string using the GlobalAlloc to allocate memory
LPTSTR GlobalStrDup(LPCTSTR source);
// duplicates a string using the HeapAlloc to allocate memory
LPTSTR HeapStrDup(HANDLE heap, LPCTSTR source);

// converts a string from UTF-8 to UTF-16 allocating
// the memory for the destination string with HeapAlloc
LPWSTR HeapStrUtf8ToWide(HANDLE heap, LPCSTR source);
// converts a string from UTF-16 to UTF-8 allocating
// the memory for the destination string with HeapAlloc
LPSTR HeapStrWideToUtf8(HANDLE heap, LPWSTR source);

#endif // WINWRAP_H
