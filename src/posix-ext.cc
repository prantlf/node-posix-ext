#include <nan.h>

#include "process-win.h"
#include "fs-win.h"
#include "posix-win.h"

using v8::Local;
using v8::Object;
using v8::String;
using v8::Boolean;
using Nan::New;
using Nan::Set;
using Nan::HandleScope;

// the add-on module-initializing entry point function
NAN_MODULE_INIT(init)
{
  // allow getting and setting common options
  HandleScope scope;
  Local<Object> options = New<Object>();
  Set(options, New<String>("populateGroupMembers").ToLocalChecked(),
    New<Boolean>(true));
  Set(target, New<String>("options").ToLocalChecked(), options);

  process_win::init(target);
  fs_win::init(target);
  posix_win::init(target);
}

// declare the add-on initializer
NODE_MODULE(posix_ext, init)
