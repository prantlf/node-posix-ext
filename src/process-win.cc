#include "process-win.h"
#include "autores.h"

#include <sddl.h>
#include <cassert>

// methods:
//   getuid, getgid, getgroups
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

namespace process_win {

using node::WinapiErrnoException;
using v8::Local;
using v8::Function;
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

// ------------------------------------------------
// internal functions to support the native exports

// gets information about the current process provided by the
// GetTokenInformation; the result needs to be freed by HeapFree
template <TOKEN_INFORMATION_CLASS C, typename T>
static DWORD get_process_info(T * result) {
  assert(result != NULL);

  // open the current process for reading the requested information
  WinHandle<HANDLE> token;
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) == FALSE)  {
    return GetLastError();
  }

  // get the size of the buffer to accomodate the requested information
  DWORD szbuffer = 0;
  if (GetTokenInformation(token, C, NULL, 0, &szbuffer) != FALSE) {
    return ERROR_INVALID_FUNCTION;
  }
  DWORD error = GetLastError();
  if (error != ERROR_INSUFFICIENT_BUFFER) {
    return error;
  }

  // allocate the buffer for the requested information
  HeapMem<T> buffer = HeapMem<T>::Allocate(szbuffer);
  if (!buffer.IsValid()) {
    return GetLastError();
  }

  // fill the buffer with the requested information
  if (GetTokenInformation(token, C, buffer, szbuffer, &szbuffer) == FALSE) {
    return GetLastError();
  }

  *result = buffer.Detach();

  return ERROR_SUCCESS;
}

// ---------------------------------------------
// getuid - gets the current process uid as SID:
// uid  getuid( [callback] )

// gets the current process user as SID;
// the uid needs to be freed by LocalFree
static DWORD getuid_impl(LPSTR * uid) {
  assert(uid != NULL);

  // get TOKEN_USER for the current process
  HeapMem<PTOKEN_USER> info;
  DWORD error = get_process_info<TokenUser>(&info);
  if (error != ERROR_SUCCESS) {
    return error;
  }

  // convert the user SID to string
  if (ConvertSidToStringSid(info->User.Sid, uid) == FALSE) {
    return GetLastError();
  }

  return ERROR_SUCCESS;
}

// passes input/output parameters between the native method entry point
// and the worker method doing the work, which is called asynchronously
class getuid_worker : public AsyncWorker {
  public:
    getuid_worker(Callback * callback) : AsyncWorker(callback) {}

    ~getuid_worker() {}

  // passes the execution to getuid_impl
  void Execute() {
    error = getuid_impl(&uid);
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
        New<String>((LPSTR) uid).ToLocalChecked()
      };
      callback->Call(2, argv);
    }
  }

  private:
    DWORD error;
    LocalMem<LPSTR> uid;
};

// the native entry point for the exposed getuid function
NAN_METHOD(getuid) {
  int argc = info.Length();
  if (argc > 1)
    return ThrowTypeError("too many arguments");
  if (argc > 0 && !info[0]->IsFunction())
    return ThrowTypeError("callback must be a function");

  // if no callback was provided, assume the synchronous scenario,
  // call the method_impl immediately and return its results
  if (!info[0]->IsFunction()) {
    HandleScope scope;
    LocalMem<LPSTR> uid;
    DWORD error = getuid_impl(&uid);
    if (error != ERROR_SUCCESS)
      return ThrowWinapiError(error);
    return info.GetReturnValue().Set(
      New<String>((LPSTR) uid).ToLocalChecked());
  }

  // prepare parameters for the method_impl to be called later;
  // queue the worker to be called when posibble and send its
  // result to the external callback
  Callback * callback = new Callback(info[0].As<Function>());
  AsyncQueueWorker(new getuid_worker(callback));
}

// ---------------------------------------------
// getgid - gets the current process gid as SID:
// gid  getgid( [callback] )

// gets the current process primary group as SID;
// the gid needs to be freed by LocalFree
static DWORD getgid_impl(LPSTR * gid) {
  assert(gid != NULL);

  // get TOKEN_PRIMARY_GROUP for the current process
  HeapMem<PTOKEN_PRIMARY_GROUP> info;
  DWORD error = get_process_info<TokenPrimaryGroup>(&info);
  if (error != ERROR_SUCCESS) {
    return error;
  }

  // convert the group SID to string
  if (ConvertSidToStringSid(info->PrimaryGroup, gid) == FALSE) {
    return GetLastError();
  }

  return ERROR_SUCCESS;
}

// passes input/output parameters between the native method entry point
// and the worker method doing the work, which is called asynchronously
class getgid_worker : public AsyncWorker {
  public:
    getgid_worker(Callback * callback) : AsyncWorker(callback) {}

    ~getgid_worker() {}

  // passes the execution to getgid_impl
  void Execute() {
    error = getgid_impl(&gid);
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
        New<String>((LPSTR) gid).ToLocalChecked()
      };
      callback->Call(2, argv);
    }
  }

  private:
    DWORD error;
    LocalMem<LPSTR> gid;
};

// the native entry point for the exposed getgid function
NAN_METHOD(getgid) {
  int argc = info.Length();
  if (argc > 1)
    return ThrowTypeError("too many arguments");
  if (argc > 0 && !info[0]->IsFunction())
    return ThrowTypeError("callback must be a function");

  // if no callback was provided, assume the synchronous scenario,
  // call the method_impl immediately and return its results
  if (!info[0]->IsFunction()) {
    HandleScope scope;
    LocalMem<LPSTR> uid;
    DWORD error = getgid_impl(&uid);
    if (error != ERROR_SUCCESS)
      return ThrowWinapiError(error);
    return info.GetReturnValue().Set(
      New<String>((LPSTR) uid).ToLocalChecked());
  }

  // prepare parameters for the method_impl to be called later;
  // queue the worker to be called when posibble and send its
  // result to the external callback
  Callback * callback = new Callback(info[0].As<Function>());
  AsyncQueueWorker(new getgid_worker(callback));
}

// ------------------------------------------------------------------
// getgroups - gets the current process supplementary groups as SIDs:
// [ gid ]  getgroups( [callback] )

// gets the current process supplementary groups as SIDs
static DWORD getgroups_impl(HeapArray<LocalMem<LPSTR> > & groups) {
  // get TOKEN_GROUPS for the current process
  HeapMem<PTOKEN_GROUPS> info;
  DWORD error = get_process_info<TokenGroups>(&info);
  if (error != ERROR_SUCCESS) {
    return error;
  }

  // convert the group SIDs to strings
  groups.Resize(info->GroupCount);
  for (int i = 0; i < groups.Size(); ++i) {
    if (ConvertSidToStringSid(info->Groups[i].Sid, &groups[i]) == FALSE) {
      return GetLastError();
    }
  }

  return ERROR_SUCCESS;
}

// converts an array of strings with the group names to the JavaScript result
static Local<Array> convert_groups(
  HeapArray<LocalMem<LPSTR> > const & groups) {
  Local<Array> result = New<Array>(groups.Size());
  if (!result.IsEmpty()) {
    for (int i = 0; i < groups.Size(); ++i) {
      Set(result, i, New<String>((LPSTR) groups[i]).ToLocalChecked());
    }
  }
  return result;
}

// passes input/output parameters between the native method entry point
// and the worker method doing the work, which is called asynchronously
class getgroups_worker : public AsyncWorker {
  public:
    getgroups_worker(Callback * callback) : AsyncWorker(callback) {}

    ~getgroups_worker() {}

  // passes the execution to getgroups_impl
  void Execute() {
    error = getgroups_impl(groups);
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
        convert_groups(groups)
      };
      callback->Call(2, argv);
    }
  }

  private:
    DWORD error;
    HeapArray<LocalMem<LPSTR> > groups;
};

// the native entry point for the exposed getgroups function
NAN_METHOD(getgroups) {
  int argc = info.Length();
  if (argc > 1)
    return ThrowTypeError("too many arguments");
  if (argc > 0 && !info[0]->IsFunction())
    return ThrowTypeError("callback must be a function");

  // if no callback was provided, assume the synchronous scenario,
  // call the method_impl immediately and return its results
  if (!info[0]->IsFunction()) {
    HandleScope scope;
    HeapArray<LocalMem<LPSTR> > groups;
    DWORD error = getgroups_impl(groups);
    if (error != ERROR_SUCCESS)
      return ThrowWinapiError(error);
    return info.GetReturnValue().Set(convert_groups(groups));
  }

  // prepare parameters for the method_impl to be called later;
  // queue the worker to be called when posibble and send its
  // result to the external callback
  Callback * callback = new Callback(info[0].As<Function>());
  AsyncQueueWorker(new getgroups_worker(callback));
}

// exposes methods implemented by this sub-package and initializes the
// string symbols for the converted resulting object literals; to be
// called from the add-on module-initializing function
NAN_MODULE_INIT(init) {
  NAN_EXPORT(target, getuid);
  NAN_EXPORT(target, getgid);
  NAN_EXPORT(target, getgroups);
}

} // namespace process_win
