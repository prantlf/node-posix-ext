#include "process-win.h"
#include "fs-win.h"
#include "posix-win.h"

// the add-on module-initializing entry point function
NAN_MODULE_INIT(init)
{
  process_win::init(target);
  fs_win::init(target);
  posix_win::init(target);
}

// declare the add-on initializer
NODE_MODULE(posix_ext, init)
