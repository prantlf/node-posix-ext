#ifdef _WIN32

#include "winwrap.h"
#include "autores.h"

#include <cassert>

using namespace autores;

// duplicates a string using the memory allocating method of a memory
// managing template descended from AutoMem
template <typename A> LPTSTR StrDup(LPCTSTR source) {
  assert(source != NULL);
  // the allocating method accepts byte size; not item count
  size_t size = (_tcslen(source) + 1) * sizeof(TCHAR);
  LPTSTR target = A::Allocate(size);
  if (target != NULL) {
    // copy including the terminating zero character
    CopyMemory(target, source, size);
  }
  return target;
}

// duplicates a string using the LocalAlloc to allocate memory
LPTSTR LocalStrDup(LPCTSTR source) {
  return StrDup<LocalMem<LPTSTR> >(source);
}

// duplicates a string using the GlobalAlloc to allocate memory
LPTSTR GlobalStrDup(LPCTSTR source) {
  return StrDup<GlobalMem<LPTSTR> >(source);
}

// duplicates a string using the HeapAlloc to allocate memory
LPTSTR HeapStrDup(HANDLE heap, LPCTSTR source) {
  assert(heap != NULL);
  assert(source != NULL);
  // the allocating method accepts byte size; not item count
  size_t size = (_tcslen(source) + 1) * sizeof(TCHAR);
  LPTSTR target = HeapMem<LPTSTR>::Allocate(size, heap);
  if (target != NULL) {
    // copy including the terminating zero character
    CopyMemory(target, source, size);
  }
  return target;
}

// converts a string from UTF-8 to UTF-16 allocating
// the memory for the destination string with HeapAlloc
LPWSTR HeapStrUtf8ToWide(HANDLE heap, LPCSTR source) {
  assert(heap != NULL);
  assert(source != NULL);
  // zero means an error; even an empty string needs a size greater
  // than zero because of the terminating zero character
  int size = MultiByteToWideChar(CP_UTF8, 0, source, -1, NULL, 0);
  if (size == 0) {
    return NULL;
  }
  HeapMem<LPWSTR> target = HeapMem<LPWSTR>::Allocate(
    size * sizeof(WCHAR), heap);
  if (target == NULL || MultiByteToWideChar(CP_UTF8, 0, source, -1,
                           target, size) == 0) {
    return NULL;
  }
  return target.Detach();
}

// converts a string from UTF-16 to UTF-8 allocating
// the memory for the destination string with HeapAlloc
LPSTR HeapStrWideToUtf8(HANDLE heap, LPWSTR source) {
  assert(heap != NULL);
  assert(source != NULL);
  // zero means an error; even an empty string needs a size greater
  // than zero because of the terminating zero character
  int size = WideCharToMultiByte(CP_UTF8, 0, source, -1, NULL, 0,
    NULL, NULL);
  if (size == 0) {
    return NULL;
  }
  HeapMem<LPSTR> target = HeapMem<LPSTR>::Allocate(size, heap);
  if (target == NULL || WideCharToMultiByte(CP_UTF8, 0, source, -1,
                           target, size, NULL, NULL) == 0) {
    return NULL;
  }
  return target.Detach();
}

#endif // _WIN32
