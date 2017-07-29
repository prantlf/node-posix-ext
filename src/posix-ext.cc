#include "process-win.h"
#include "fs-win.h"
#include "posix-win.h"

using namespace node;
using namespace v8;

// the add-on module-initializing entry point function
extern "C" void init(Handle<Object> target)
{
#if NODE_MODULE_VERSION > NODE_0_10_MODULE_VERSION
  HandleScope scope(Isolate::GetCurrent());
#else
  HandleScope scope;
#endif

  // initialize all sub-packages of this add-on module
  process_win::init(target);
  fs_win::init(target);
  posix_win::init(target);
}

// declare the add-on initializer
NODE_MODULE(posix_ext, init)
