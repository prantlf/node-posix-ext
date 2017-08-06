#include "fs-win.h"
#include "autores.h"
#include "winwrap.h"

#include <io.h>
#include <accctrl.h>
#include <aclapi.h>
#include <sddl.h>

// methods:
//   fgetown, getown,
//   fchown, chown
//
// method implementation pattern:
//
// register method as exports.method
// method {
//   if sync:  call method_impl, return convert_result
//   if async: queue worker
// }
// method_impl {
//   perform native code
// }
// worker {
//   execute method_impl, return convert_result to callback
// }

namespace fs_win {

using node::WinapiErrnoException;
using v8::Local;
using v8::Function;
using v8::Object;
using v8::Array;
using v8::Value;
using v8::String;
using Nan::AsyncQueueWorker;
using Nan::AsyncWorker;
using Nan::Callback;
using Nan::HandleScope;
using Nan::ThrowError;
using Nan::ThrowTypeError;
using Nan::New;
using Nan::Null;
using Nan::Undefined;
using Nan::EmptyString;
using Nan::Set;
using namespace autores;

// helpers for returning errors from native methods
#if (NODE_MAJOR_VERSION > 0) || (NODE_MINOR_VERSION > 10)
  #define WinapiError(error) \
    WinapiErrnoException(v8::Isolate::GetCurrent(), error)
#else
  #define WinapiError(error) \
    WinapiErrnoException(error)
#endif
#define ThrowWinapiError(error) \
  ThrowError(WinapiError(error))
#define ThrowLastWinapiError() \
  ThrowWinapiError(GetLastError())

// ------------------------------------------------
// internal functions to support the native exports

// class helper to enable and disable taking object ownership in this
// process; it's used explicitly by calling Enable and Disable to be
// able to check for errors, but it supports RAII too for error cases
class TakingOwhership {
  private:
    WinHandle<HANDLE> process;
    bool enabled;

    // changes the privileges necessary for taking ownership
    // in the current process - either enabling or disabling it
    BOOL SetPrivileges(BOOL enable) {
      LPCTSTR const names[] = {
        SE_TAKE_OWNERSHIP_NAME, SE_SECURITY_NAME,
        SE_BACKUP_NAME, SE_RESTORE_NAME
      };

      HeapMem<PTOKEN_PRIVILEGES> privileges =
        HeapMem<PTOKEN_PRIVILEGES>::Allocate(FIELD_OFFSET(
          TOKEN_PRIVILEGES, Privileges[sizeof(names) / sizeof(names[0])]));
      if (privileges == NULL) {
        return FALSE;
      }
      privileges->PrivilegeCount = sizeof(names) / sizeof(names[0]);
      for (size_t i = 0; i < privileges->PrivilegeCount; ++i) {
        if (LookupPrivilegeValue(NULL, names[i],
            &privileges->Privileges[i].Luid) == FALSE) {
          return FALSE;
        }
        privileges->Privileges[i].Attributes =
          enable != FALSE ? SE_PRIVILEGE_ENABLED : 0;
      }

      if (AdjustTokenPrivileges(process, FALSE, privileges,
          sizeof(privileges), NULL, NULL) == FALSE) {
        return FALSE;
      }
      if (GetLastError() == ERROR_NOT_ALL_ASSIGNED) {
        SetLastError(ERROR_NOT_ALL_ASSIGNED);
        return FALSE;
      }

      return TRUE;
    }

  public:
    TakingOwhership() : enabled(false) {}

    ~TakingOwhership() {
      Disable();
    }

    DWORD Enable() {
      if (OpenProcessToken(GetCurrentProcess(),
          TOKEN_ADJUST_PRIVILEGES, &process) == FALSE)  {
        return GetLastError();
      }
      if (SetPrivileges(TRUE) == FALSE) {
        return GetLastError();
      }
      enabled = true;
      return ERROR_SUCCESS;
    }

    DWORD Disable() {
      if (enabled) {
        if (SetPrivileges(FALSE) == FALSE) {
          return GetLastError();
        }
        if (!process.Dispose()) {
          return GetLastError();
        }
        enabled = false;
      }
      return ERROR_SUCCESS;
    }
};

// makes a JavaScript result object literal of user and group SIDs
static Local<Value> convert_ownership(LPSTR uid, LPSTR gid) {
  Local<Object> result = New<Object>();
  if (!result.IsEmpty()) {
    Set(result, New<String>("uid").ToLocalChecked(),
      New<String>(uid).ToLocalChecked());
    Set(result, New<String>("gid").ToLocalChecked(),
      New<String>(gid).ToLocalChecked());
  }
  return result;
}

// -------------------------------------------------------
// fgetown - gets the file or directory ownership as SIDs:
// { uid, gid }  fgetown( fd, [callback] )

static int fgetown_impl(int fd, LPSTR *uid, LPSTR *gid) {
  assert(uid != NULL);
  assert(gid != NULL);

  HANDLE fh = (HANDLE) _get_osfhandle(fd);

  PSID usid = NULL, gsid = NULL;
  LocalMem<PSECURITY_DESCRIPTOR> sd;
  DWORD error = GetSecurityInfo(fh, SE_FILE_OBJECT,
    OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION,
    &usid, &gsid, NULL, NULL, &sd);
  if (error != ERROR_SUCCESS) {
    return error;
  }

  LocalMem<LPSTR> susid;
  if (ConvertSidToStringSid(usid, &susid) == FALSE) {
    return GetLastError();
  }

  LocalMem<LPSTR> sgsid;
  if (ConvertSidToStringSid(gsid, &sgsid) == FALSE) {
    return GetLastError();
  }

  *uid = susid.Detach();
  *gid = sgsid.Detach();

  return ERROR_SUCCESS;
}

// passes input/output parameters between the native method entry point
// and the worker method doing the work, which is called asynchronously
class fgetown_worker : public AsyncWorker {
  public:
    fgetown_worker(Callback * callback, int fd)
    : AsyncWorker(callback), fd(fd) {}

    ~fgetown_worker() {}

  // passes the execution to fgetown_impl
  void Execute() {
    error = fgetown_impl(fd, &susid, &sgsid);
  }

  // called after an asynchronously called method (method_impl) has
  // finished to convert the results to JavaScript objects and pass
  // them to JavaScript callback
  void HandleOKCallback() {
    HandleScope scope;
    if (error != ERROR_SUCCESS) {
      // pass the error to the external callback
      Local<Value> argv[] = {
        // in case of error, make the first argument an error object
        WinapiError(error)
      };
      callback->Call(1, argv);
    } else {
      // pass the results to the external callback
      Local<Value> argv[] = {
        // in case of success, make the first argument (error) null
        Null(),
        // in case of success, populate the second and other arguments
        convert_ownership(susid, sgsid)
      };
      callback->Call(2, argv);
    }
  }

  private:
    DWORD error;
    int fd;
    LocalMem<LPSTR> susid, sgsid;
};

// the native entry point for the exposed fgetown function
NAN_METHOD(fgetown) {
  int argc = info.Length();
  if (argc < 1)
    return ThrowTypeError("fd required");
  if (argc > 2)
    return ThrowTypeError("too many arguments");
  if (!info[0]->IsInt32())
    return ThrowTypeError("fd must be an int");
  if (argc > 1 && !info[1]->IsFunction())
    return ThrowTypeError("callback must be a function");

  int fd = info[0]->Int32Value();
  
  // if no callback was provided, assume the synchronous scenario,
  // call the method_sync immediately and return its results
  if (!info[1]->IsFunction()) {
    HandleScope scope;
    LocalMem<LPSTR> susid, sgsid;
    DWORD error = fgetown_impl(fd, &susid, &sgsid);
    if (error != ERROR_SUCCESS)
      return ThrowWinapiError(error);
    return info.GetReturnValue().Set(convert_ownership(susid, sgsid));
  }

  // prepare parameters for the method_impl to be called later;
  // queue the worker to be called when posibble and send its
  // result to the external callback
  Callback * callback = new Callback(info[1].As<Function>());
  AsyncQueueWorker(new fgetown_worker(callback, fd));
}

// ------------------------------------------------------
// getown - gets the file or directory ownership as SIDs:
// { uid, gid }  getown( path, [callback] )

// gets the file ownership (uid and gid) for the file path
static int getown_impl(LPCSTR path, LPSTR *uid, LPSTR *gid) {
  assert(path != NULL);
  assert(uid != NULL);
  assert(gid != NULL);

  PSID usid = NULL, gsid = NULL;
  LocalMem<PSECURITY_DESCRIPTOR> sd;
  DWORD error = GetNamedSecurityInfo((LPSTR) path, SE_FILE_OBJECT,
    OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION,
    &usid, &gsid, NULL, NULL, &sd);
  if (error != ERROR_SUCCESS) {
    return error;
  }

  LocalMem<LPSTR> susid;
  if (ConvertSidToStringSid(usid, &susid) == FALSE) {
    return GetLastError();
  }

  LocalMem<LPSTR> sgsid;
  if (ConvertSidToStringSid(gsid, &sgsid) == FALSE) {
    return GetLastError();
  }

  *uid = susid.Detach();
  *gid = sgsid.Detach();

  return ERROR_SUCCESS;
}

// passes input/output parameters between the native method entry point
// and the worker method doing the work, which is called asynchronously
class getown_worker : public AsyncWorker {
  public:
    getown_worker(Callback * callback, LPSTR name) : AsyncWorker(callback) {
      path = HeapStrDup(HeapBase::ProcessHeap(), name);
      error = path.IsValid() ? ERROR_SUCCESS : GetLastError();
    }

    ~getown_worker() {}

  // passes the execution to getown_impl
  void Execute() {
    if (error == ERROR_SUCCESS) {
      error = getown_impl((LPSTR) path, &susid, &sgsid);
    }
  }

  // called after an asynchronously called method (method_impl) has
  // finished to convert the results to JavaScript objects and pass
  // them to JavaScript callback
  void HandleOKCallback() {
    HandleScope scope;
    if (error != ERROR_SUCCESS) {
      // pass the error to the external callback
      Local<Value> argv[] = {
        // in case of error, make the first argument an error object
        WinapiError(error)
      };
      callback->Call(1, argv);
    } else {
      // pass the results to the external callback
      Local<Value> argv[] = {
        // in case of success, make the first argument (error) null
        Null(),
        // in case of success, populate the second and other arguments
        convert_ownership(susid, sgsid)
      };
      callback->Call(2, argv);
    }
  }

  private:
    DWORD error;
    HeapMem<LPSTR> path;
    LocalMem<LPSTR> susid, sgsid;
};

// the native entry point for the exposed getown function
NAN_METHOD(getown) {
  int argc = info.Length();
  if (argc < 1)
    return ThrowTypeError("path required");
  if (argc > 2)
    return ThrowTypeError("too many arguments");
  if (!info[0]->IsString())
    return ThrowTypeError("path must be a string");
  if (argc > 1 && !info[1]->IsFunction())
    return ThrowTypeError("callback must be a function");

  String::Utf8Value path(info[0]->ToString());
  
  // if no callback was provided, assume the synchronous scenario,
  // call the method_sync immediately and return its results
  if (!info[1]->IsFunction()) {
    HandleScope scope;
    LocalMem<LPSTR> susid, sgsid;
    DWORD error = getown_impl(*path, &susid, &sgsid);
    if (error != ERROR_SUCCESS)
      return ThrowWinapiError(error);
    return info.GetReturnValue().Set(convert_ownership(susid, sgsid));
  }

  // prepare parameters for the method_impl to be called later;
  // queue the worker to be called when posibble and send its
  // result to the external callback
  Callback * callback = new Callback(info[1].As<Function>());
  AsyncQueueWorker(new getown_worker(callback, *path));
}

// --------------------------------------------------------
// fchown - sets the file or directory ownership with SIDs:
// fchown( fd, uid, gid, [callback] )

// change the ownership (uid and gid) of the file specified by the
// file descriptor; either uid or gid can be empty ("") to change
// just one of them
static int fchown_impl(int fd, LPCSTR uid, LPCSTR gid) {
  assert(uid != NULL);
  assert(gid != NULL);

  // get the OS file handle for the specified file descriptor
  HANDLE fh = (HANDLE) _get_osfhandle(fd);

  // convert the input SIDs from strings to SID structures
  LocalMem<PSID> usid;
  if (*uid && ConvertStringSidToSid(uid, &usid) == FALSE) {
    return GetLastError();
  }
  LocalMem<PSID> gsid;
  if (*gid && ConvertStringSidToSid(gid, &gsid) == FALSE) {
    return GetLastError();
  }

  // enable taking object ownership in the current process
  // if the effective user has enough permissions
  TakingOwhership takingOwhership;
  DWORD error = takingOwhership.Enable();
  if (error != ERROR_SUCCESS) {
    return error;
  }

  // take ownership of the object specified by the file handle
  if (*uid && *gid) {
    if (SetSecurityInfo(fh, SE_FILE_OBJECT,
          OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION,
          usid, gsid, NULL, NULL) != ERROR_SUCCESS) {
      return GetLastError();
    }
  } else if (*uid) {
    if (SetSecurityInfo(fh, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
          usid, NULL, NULL, NULL) != ERROR_SUCCESS) {
      return GetLastError();
    }
  } else if (*gid) {
    if (SetSecurityInfo(fh, SE_FILE_OBJECT, GROUP_SECURITY_INFORMATION,
          NULL, gsid, NULL, NULL) != ERROR_SUCCESS) {
      return GetLastError();
    }
  }

  // disnable taking object ownership in the current process
  // not to leak the availability of this privileged operation
  error = takingOwhership.Disable();
  if (error != ERROR_SUCCESS) {
    return error;
  }

  return ERROR_SUCCESS;
}

// passes input/output parameters between the native method entry point
// and the worker method doing the work, which is called asynchronously
class fchown_worker : public AsyncWorker {
  public:
    fchown_worker(Callback * callback, int fd, LPSTR uid, LPSTR gid)
    : AsyncWorker(callback), fd(fd) {
      susid = LocalStrDup(uid);
      error = susid.IsValid() ? ERROR_SUCCESS : GetLastError();
      if (error == ERROR_SUCCESS) {
        sgsid = LocalStrDup(gid);
        error = sgsid.IsValid() ? ERROR_SUCCESS : GetLastError();
      }
    }

    ~fchown_worker() {}

  // passes the execution to fchown_impl
  void Execute() {
    if (error == ERROR_SUCCESS) {
      error = fchown_impl(fd, susid, sgsid);
    }
  }

  // called after an asynchronously called method (method_impl) has
  // finished to convert the results to JavaScript objects and pass
  // them to JavaScript callback
  void HandleOKCallback() {
    HandleScope scope;
    if (error != ERROR_SUCCESS) {
      // pass the error to the external callback
      Local<Value> argv[] = {
        // in case of error, make the first argument an error object
        WinapiError(error)
      };
      callback->Call(1, argv);
    } else {
      // pass the results to the external callback
      Local<Value> argv[] = {
        // in case of success, make the first argument (error) null
        Null(),
        // in case of success, populate the second and other arguments
        Undefined()
      };
      callback->Call(2, argv);
    }
  }

  private:
    DWORD error;
    int fd;
    LocalMem<LPSTR> susid, sgsid;
};

// the native entry point for the exposed fchown function
NAN_METHOD(fchown) {
  int argc = info.Length();
  if (argc < 1)
    return ThrowTypeError("fd required");
  if (argc > 4)
    return ThrowTypeError("too many arguments");
  if (!info[0]->IsInt32())
    return ThrowTypeError("fd must be an int");
  if (argc < 2)
    return ThrowTypeError("uid required");
  if (!info[1]->IsString() && !info[1]->IsUndefined())
    return ThrowTypeError("uid must be a string or undefined");
  if (argc < 3)
    return ThrowTypeError("gid required");
  if (!info[2]->IsString() && !info[2]->IsUndefined())
    return ThrowTypeError("gid must be a string or undefined");
  if (argc > 3 && !info[3]->IsFunction())
    return ThrowTypeError("callback must be a function");
  if (info[1]->IsUndefined() && info[2]->IsUndefined())
    return ThrowTypeError("either uid or gid must be defined");

  int fd = info[0]->Int32Value();
  String::Utf8Value susid(info[1]->IsString() ?
    info[1]->ToString() : EmptyString());
  String::Utf8Value sgsid(info[2]->IsString() ?
    info[2]->ToString() : EmptyString());

  // if no callback was provided, assume the synchronous scenario,
  // call the method_sync immediately and return its results
  if (!info[3]->IsFunction()) {
    HandleScope scope;
    DWORD error = fchown_impl(fd, *susid, *sgsid);
    if (error != ERROR_SUCCESS)
      return ThrowWinapiError(error);
    return info.GetReturnValue().Set(Undefined());
  }

  // prepare parameters for the method_impl to be called later;
  // queue the worker to be called when posibble and send its
  // result to the external callback
  Callback * callback = new Callback(info[1].As<Function>());
  AsyncQueueWorker(new fchown_worker(callback, fd, *susid, *sgsid));
}

// -------------------------------------------------------
// chown - sets the file or directory ownership with SIDs:
// chown( name, uid, gid, [callback] )

// change the ownership (uid and gid) of the file specified by the
// file path; either uid or gid can be empty ("") to change
// just one of them
static int chown_impl(LPCSTR path, LPCSTR uid, LPCSTR gid) {
  assert(path != NULL);
  assert(uid != NULL);
  assert(gid != NULL);

  // convert the input SIDs from strings to SID structures
  LocalMem<PSID> usid;
  if (*uid && ConvertStringSidToSid(uid, &usid) == FALSE) {
    return GetLastError();
  }
  LocalMem<PSID> gsid;
  if (*gid && ConvertStringSidToSid(gid, &gsid) == FALSE) {
    return GetLastError();
  }

  // enable taking object ownership in the current process
  // if the effective user has enough permissions
  TakingOwhership takingOwhership;
  DWORD error = takingOwhership.Enable();
  if (error != ERROR_SUCCESS) {
    return error;
  }

  // take ownership of the object specified by its path
  if (*uid && *gid) {
    if (SetNamedSecurityInfo(const_cast<LPSTR>(path), SE_FILE_OBJECT,
          OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION,
          usid, gsid, NULL, NULL) != ERROR_SUCCESS) {
      return GetLastError();
    }
  } else if (*uid) {
    if (SetNamedSecurityInfo(const_cast<LPSTR>(path),
          SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, 
          usid, gsid, NULL, NULL) != ERROR_SUCCESS) {
      return GetLastError();
    }
  } else if (*gid) {
    if (SetNamedSecurityInfo(const_cast<LPSTR>(path),
          SE_FILE_OBJECT, GROUP_SECURITY_INFORMATION,
          NULL, gsid, NULL, NULL) != ERROR_SUCCESS) {
      return GetLastError();
    }
  }

  // disnable taking object ownership in the current process
  // not to leak the availability of this privileged operation
  error = takingOwhership.Disable();
  if (error != ERROR_SUCCESS) {
    return error;
  }

  return ERROR_SUCCESS;
}

// passes input/output parameters between the native method entry point
// and the worker method doing the work, which is called asynchronously
class chown_worker : public AsyncWorker {
  public:
    chown_worker(Callback * callback, LPSTR name, LPSTR uid, LPSTR gid)
    : AsyncWorker(callback) {
      path = HeapStrDup(HeapBase::ProcessHeap(), name);
      error = path.IsValid() ? ERROR_SUCCESS : GetLastError();
      if (error == ERROR_SUCCESS) {
        susid = LocalStrDup(uid);
        error = susid.IsValid() ? ERROR_SUCCESS : GetLastError();
        if (error == ERROR_SUCCESS) {
          sgsid = LocalStrDup(gid);
          error = sgsid.IsValid() ? ERROR_SUCCESS : GetLastError();
        }
      }
    }

    ~chown_worker() {}

  // passes the execution to chown_impl
  void Execute() {
    if (error == ERROR_SUCCESS) {
      error = chown_impl((LPSTR) path, susid, sgsid);
    }
  }

  // called after an asynchronously called method (method_impl) has
  // finished to convert the results to JavaScript objects and pass
  // them to JavaScript callback
  void HandleOKCallback() {
    HandleScope scope;
    if (error != ERROR_SUCCESS) {
      // pass the error to the external callback
      Local<Value> argv[] = {
        // in case of error, make the first argument an error object
        WinapiError(error)
      };
      callback->Call(1, argv);
    } else {
      // pass the results to the external callback
      Local<Value> argv[] = {
        // in case of success, make the first argument (error) null
        Null(),
        // in case of success, populate the second and other arguments
        Undefined()
      };
      callback->Call(2, argv);
    }
  }

  private:
    DWORD error;
    HeapMem<LPSTR> path;
    LocalMem<LPSTR> susid, sgsid;
};

// the native entry point for the exposed chown function
NAN_METHOD(chown) {
  int argc = info.Length();
  if (argc < 1)
    return ThrowTypeError("path required");
  if (argc > 4)
    return ThrowTypeError("too many arguments");
  if (!info[0]->IsString())
    return ThrowTypeError("path must be a string");
  if (argc < 2)
    return ThrowTypeError("uid required");
  if (!info[1]->IsString() && !info[1]->IsUndefined())
    return ThrowTypeError("uid must be a string or undefined");
  if (argc < 3)
    return ThrowTypeError("gid required");
  if (!info[2]->IsString() && !info[2]->IsUndefined())
    return ThrowTypeError("gid must be a string or undefined");
  if (argc > 3 && !info[3]->IsFunction())
    return ThrowTypeError("callback must be a function");
  if (info[1]->IsUndefined() && info[2]->IsUndefined())
    return ThrowTypeError("either uid or gid must be defined");

  String::Utf8Value path(info[0]->ToString());
  String::Utf8Value susid(info[1]->IsString() ?
    info[1]->ToString() : EmptyString());
  String::Utf8Value sgsid(info[2]->IsString() ?
    info[2]->ToString() : EmptyString());

  // if no callback was provided, assume the synchronous scenario,
  // call the method_sync immediately and return its results
  if (!info[3]->IsFunction()) {
      HandleScope scope;
    DWORD error = chown_impl(*path, *susid, *sgsid);
    if (error != ERROR_SUCCESS)
      return ThrowWinapiError(error);
    return info.GetReturnValue().Set(Undefined());
  }

  // prepare parameters for the method_impl to be called later;
  // queue the worker to be called when posibble and send its
  // result to the external callback
  Callback * callback = new Callback(info[1].As<Function>());
  AsyncQueueWorker(new chown_worker(callback, *path, *susid, *sgsid));
}

// exposes methods implemented by this sub-package and initializes the
// string symbols for the converted resulting object literals; to be
// called from the add-on module-initializing function
NAN_MODULE_INIT(init) {
  NAN_EXPORT(target, fgetown);
  NAN_EXPORT(target, getown);
  NAN_EXPORT(target, fchown);
  NAN_EXPORT(target, chown);
}

} // namespace fs_win
