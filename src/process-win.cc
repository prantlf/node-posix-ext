#ifdef _WIN32

#include "process-win.h"
#include "autores.h"

#include <sddl.h>
#include <cassert>

// methods:
//   getuid, getgid, getgroups
//
// method implementation pattern:
//
// register method_impl as exports.method
// method_impl {
//   if sync:  call method_sync, return convert_result
//   if async: queue method_async with after_async
// }
// method_async {
//   call method_sync
// }
// after_async {
//   for OPERATION_METHOD: return convert_result to callback
// }

namespace process_win {

using namespace node;
using namespace v8;
using namespace autores;

// helpers for returning errors from native methods
#define THROW_TYPE_ERROR(msg) \
  ThrowException(Exception::TypeError(String::New(msg)))
#define LAST_WINAPI_ERROR \
  GetLastError()
#define THROW_WINAPI_ERROR(err) \
  ThrowException(WinapiErrnoException(err))
#define THROW_LAST_WINAPI_ERROR \
  THROW_WINAPI_ERROR(LAST_WINAPI_ERROR)

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
    return LAST_WINAPI_ERROR;
  }

  // get the size of the buffer to accomodate the requested information
  DWORD szbuffer = 0;
  if (GetTokenInformation(token, C, NULL, 0, &szbuffer) != FALSE) {
    return ERROR_INVALID_FUNCTION;
  }
  DWORD error = LAST_WINAPI_ERROR;
  if (error != ERROR_INSUFFICIENT_BUFFER) {
    return error;
  }

  // allocate the buffer for the requested information
  HeapMem<T> buffer = HeapMem<T>::Allocate(szbuffer);
  if (!buffer.IsValid()) {
    return LAST_WINAPI_ERROR;
  }

  // fill the buffer with the requested information
  if (GetTokenInformation(token, C, buffer, szbuffer, &szbuffer) == FALSE) {
    return LAST_WINAPI_ERROR;
  }

  *result = buffer.Detach();
  return ERROR_SUCCESS;
}

// -----------------------------------------
// support for asynchronous method execution

// codes of exposed native methods
typedef enum {
  OPERATION_GETUID,
  OPERATION_GETGID,
  OPERATION_GETGROUPS
} operation_t;

// passes input/output parameters between the native method entry point
// and the worker method doing the work, which is called asynchronously
struct async_data_t {
  uv_work_t request;
  Persistent<Function> callback;
  DWORD error;

  operation_t operation;
  LocalMem<LPSTR> uid;
  HeapArray<LocalMem<LPSTR> > groups;

  async_data_t(Local<Function> lcallback) {
    if (!lcallback.IsEmpty()) {
      callback = Persistent<Function>::New(lcallback);
    }
    request.data = this;
  }

  ~async_data_t() {
    if (!callback.IsEmpty()) {
      callback.Dispose();
    }
  }
};

// converts an array of strings with the group names to the JavaScript result
static Local<Array> convert_groups(
    HeapArray<LocalMem<LPSTR> > const & groups) {
  Local<Array> result = Array::New(groups.Size());
  if (!result.IsEmpty()) {
    for (int i = 0; i < groups.Size(); ++i) {
      result->Set(i, String::New(groups[i]));
    }
  }
  return result;
}

// called after an asynchronously called method (method_sync) has
// finished to convert the results to JavaScript objects and pass
// them to JavaScript callback
static void after_async(uv_work_t * req) {
  assert(req != NULL);
  HandleScope scope;

  Local<Value> argv[2];
  int argc = 1;

  CppObj<async_data_t *> async_data = static_cast<async_data_t *>(req->data);
  if (async_data->error != ERROR_SUCCESS) {
    argv[0] = WinapiErrnoException(async_data->error);
  } else {
    // in case of success, make the first argument (error) null
    argv[0] = Local<Value>::New(Null());
    // in case of success, populate the second and other arguments
    switch (async_data->operation) {
      case OPERATION_GETUID:
      case OPERATION_GETGID: {
        argv[1] = String::New(async_data->uid);
        argc = 2;
        break;
      }
      case OPERATION_GETGROUPS: {
        argv[1] = convert_groups(async_data->groups);
        argc = 2;
        break;
      }
      default:
        assert(FALSE && "Unknown operation");
    }
  }

  // pass the results to the external callback
  TryCatch tryCatch;
  async_data->callback->Call(Context::GetCurrent()->Global(), argc, argv);
  if (tryCatch.HasCaught()) {
    FatalException(tryCatch);
  }
}

// ---------------------------------------------
// getuid - gets the current process uid as SID:
// uid  getuid( [callback] )

// gets the current process user as SID;
// the uid needs to be freed by LocalFree
static DWORD getuid_sync(LPSTR * uid) {
  assert(uid != NULL);

  // get TOKEN_USER for the current process
  HeapMem<PTOKEN_USER> info;
  DWORD error = get_process_info<TokenUser>(&info);
  if (error != ERROR_SUCCESS) {
    return error;
  }

  // convert the user SID to string
  if (ConvertSidToStringSid(info->User.Sid, uid) == FALSE) {
    return LAST_WINAPI_ERROR;
  }

  return ERROR_SUCCESS;
}

// passes the execution to getuid_sync; the results will be processed
// by after_async
static void getuid_async(uv_work_t * req) {
  assert(req != NULL);
  async_data_t * async_data = static_cast<async_data_t *>(req->data);
  async_data->error = getuid_sync(&async_data->uid);
}

// the native entry point for the exposed getuid function
static Handle<Value> getuid_impl(Arguments const & args) {
  HandleScope scope;

  int argc = args.Length();
  if (argc > 1)
    return THROW_TYPE_ERROR("too many arguments");
  if (argc > 0 && !args[0]->IsFunction())
    return THROW_TYPE_ERROR("callback must be a function");

  // if no callback was provided, assume the synchronous scenario,
  // call the method_sync immediately and return its results
  if (!args[0]->IsFunction()) {
    LocalMem<LPSTR> uid;
    DWORD error = getuid_sync(&uid);
    if (error != ERROR_SUCCESS) {
      return THROW_WINAPI_ERROR(error);
    }
    return scope.Close(String::New(uid));
  }

  // prepare parameters for the method_sync to be called later
  // from the method_async called from the worker thread
  CppObj<async_data_t *> async_data = new async_data_t(
    Local<Function>::Cast(args[0]));
  async_data->operation = OPERATION_GETUID;

  // queue the method_async to be called when posibble and
  // after_async to send its result to the external callback
  uv_queue_work(uv_default_loop(), &async_data->request,
    getuid_async, (uv_after_work_cb) after_async);

  async_data.Detach();
  return Undefined();
}

// ---------------------------------------------
// getgid - gets the current process gid as SID:
// gid  getgid( [callback] )

// gets the current process primary group as SID;
// the gid needs to be freed by LocalFree
static DWORD getgid_sync(LPSTR * gid) {
  assert(gid != NULL);

  // get TOKEN_PRIMARY_GROUP for the current process
  HeapMem<PTOKEN_PRIMARY_GROUP> info;
  DWORD error = get_process_info<TokenPrimaryGroup>(&info);
  if (error != ERROR_SUCCESS) {
    return error;
  }

  // convert the group SID to string
  if (ConvertSidToStringSid(info->PrimaryGroup, gid) == FALSE) {
    return LAST_WINAPI_ERROR;
  }

  return ERROR_SUCCESS;
}

// passes the execution to getgid_sync; the results will be processed
// by after_async
static void getgid_async(uv_work_t * req) {
  assert(req != NULL);
  async_data_t * async_data = static_cast<async_data_t *>(req->data);
  async_data->error = getgid_sync(&async_data->uid);
}

// the native entry point for the exposed getgid function
static Handle<Value> getgid_impl(Arguments const & args) {
  HandleScope scope;

  int argc = args.Length();
  if (argc > 1)
    return THROW_TYPE_ERROR("too many arguments");
  if (argc > 0 && !args[0]->IsFunction())
    return THROW_TYPE_ERROR("callback must be a function");

  // if no callback was provided, assume the synchronous scenario,
  // call the method_sync immediately and return its results
  if (!args[0]->IsFunction()) {
    LocalMem<LPSTR> gid;
    DWORD error = getgid_sync(&gid);
    if (error != ERROR_SUCCESS) {
      return THROW_WINAPI_ERROR(error);
    }
    return scope.Close(String::New(gid));
  }

  // prepare parameters for the method_sync to be called later
  // from the method_async called from the worker thread
  CppObj<async_data_t *> async_data = new async_data_t(
    Local<Function>::Cast(args[0]));
  async_data->operation = OPERATION_GETGID;

  // queue the method_async to be called when posibble and
  // after_async to send its result to the external callback
  uv_queue_work(uv_default_loop(), &async_data->request,
    getgid_async, (uv_after_work_cb) after_async);

  async_data.Detach();
  return Undefined();
}

// ------------------------------------------------------------------
// getgroups - gets the current process supplementary groups as SIDs:
// [ gid ]  getgroups( [callback] )

// gets the current process supplementary groups as SIDs
static DWORD getgroups_sync(HeapArray<LocalMem<LPSTR> > & groups) {
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
      return LAST_WINAPI_ERROR;
    }
  }

  return ERROR_SUCCESS;
}

// passes the execution to getgroups_sync; the results will be processed
// by after_async
static void getgroups_async(uv_work_t * req) {
  assert(req != NULL);
  async_data_t * async_data = static_cast<async_data_t *>(req->data);
  async_data->error = getgroups_sync(async_data->groups);
}

// the native entry point for the exposed getgroups function
static Handle<Value> getgroups_impl(Arguments const & args) {
  HandleScope scope;

  int argc = args.Length();
  if (argc > 1)
    return THROW_TYPE_ERROR("too many arguments");
  if (argc > 0 && !args[0]->IsFunction())
    return THROW_TYPE_ERROR("callback must be a function");

  // if no callback was provided, assume the synchronous scenario,
  // call the method_sync immediately and return its results
  if (!args[0]->IsFunction()) {
    HeapArray<LocalMem<LPSTR> > groups;
    DWORD error = getgroups_sync(groups);
    if (error != ERROR_SUCCESS) {
      return THROW_WINAPI_ERROR(error);
    }
    Handle<Array> result = convert_groups(groups);
    return scope.Close(result);
  }

  // prepare parameters for the method_sync to be called later
  // from the method_async called from the worker thread
  CppObj<async_data_t *> async_data = new async_data_t(
    Local<Function>::Cast(args[0]));
  async_data->operation = OPERATION_GETGROUPS;

  // queue the method_async to be called when posibble and
  // after_async to send its result to the external callback
  uv_queue_work(uv_default_loop(), &async_data->request,
    getgroups_async, (uv_after_work_cb) after_async);

  async_data.Detach();
  return Undefined();
}

// exposes methods implemented by this sub-package; to be
// called from the add-on module-initializing function
void init(Handle<Object> target) {
  HandleScope scope;

  NODE_SET_METHOD(target, "getuid", getuid_impl);
  NODE_SET_METHOD(target, "getgid", getgid_impl);
  NODE_SET_METHOD(target, "getgroups", getgroups_impl);
}

} // namespace process_win

#endif // _WIN32
