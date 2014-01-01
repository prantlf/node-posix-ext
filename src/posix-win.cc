#ifdef _WIN32

#include "posix-win.h"
#include "autores.h"
#include "winwrap.h"

#include <sddl.h>
#include <cassert>

// methods:
//   getgrgid, getgrnam
//   getpwnam, getpwuid
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

namespace posix_win {

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

// members names of result object literals
static Persistent<String> name_symbol;
static Persistent<String> passwd_symbol;
static Persistent<String> uid_symbol;
static Persistent<String> gid_symbol;
static Persistent<String> gecos_symbol;
static Persistent<String> shell_symbol;
static Persistent<String> dir_symbol;
static Persistent<String> members_symbol;

// ------------------------------------------------
// internal functions to support the native exports

// encapsulates input/output parameters of user-handling methods
// (getpwnam, getpwuid); used in method_sync and async_data_t
struct user_t {
  LocalMem<LPSTR> uid, gid;
  HeapMem<LPSTR> name, passwd, gecos, shell, dir;
};

// encapsulates input/output parameters of group-handling methods
// (getgrnam, getgruid); used in method_sync and async_data_t
struct group_t {
  LocalMem<LPSTR> gid;
  HeapMem<LPSTR> name, passwd;
  HeapArray<HeapMem<LPSTR> > members;
};

// resolves the account name in the format "account" or "domain\account"
// to its SID; the id parameter must be freed by LocalFree whe not needed
static DWORD resolve_name(LPCSTR name, PSID * id) {
  assert(name != NULL);
  assert(id != NULL);

  // get sizes of buffers to accomodate the domain name and SID
  DWORD szsid = 0, szdomain = 0;
  SID_NAME_USE sidtype = SidTypeUnknown;
  if (LookupAccountName(NULL, name, NULL, &szsid,
      NULL, &szdomain, &sidtype) != FALSE) {
    return ERROR_INVALID_FUNCTION;
  }
  DWORD error = LAST_WINAPI_ERROR;
  if (error != ERROR_INSUFFICIENT_BUFFER) {
    return error;
  }

  // allocate the buffer for SID
  HeapMem<PSID> sid = HeapMem<PSID>::Allocate(szsid);
  if (!sid.IsValid()) {
    return LAST_WINAPI_ERROR;
  }
  // allocate the buffer for the source domain
  HeapMem<LPSTR> domain = HeapMem<LPSTR>::Allocate(szdomain);
  if (!domain.IsValid()) {
    return LAST_WINAPI_ERROR;
  }

  // get the SID and the source domain name; the latter is not needed
  // but it is always returned
  if (LookupAccountName(NULL, name, sid, &szsid,
      domain, &szdomain, &sidtype) == FALSE) {
    return LAST_WINAPI_ERROR;
  }

  *id = sid.Detach();

  return ERROR_SUCCESS;
}

// completes the group information using the gid (SID) member of it
static DWORD resolve_group(group_t & group, PSID gsid) {
  assert(gsid != NULL);

  // convert the input SID to string; although the string could be the
  // original input parameter, this will ensure always consistent ouptut
  if (ConvertSidToStringSid(gsid, &group.gid) == FALSE) {
    return LAST_WINAPI_ERROR;
  }

  // get sizes of buffers to accomodate domain and account names
  DWORD szaccount = 0, szdomain = 0;
  SID_NAME_USE sidtype = SidTypeUnknown;
  if (LookupAccountSid(NULL, gsid, NULL, &szaccount,
      NULL, &szdomain, &sidtype) != FALSE) {
    return ERROR_INVALID_FUNCTION;
  }
  DWORD error = LAST_WINAPI_ERROR;
  if (error != ERROR_INSUFFICIENT_BUFFER) {
    return error;
  }

  // allocate the buffer for both domain and account names; they
  // will be formatted "domain\account"
  group.name = HeapMem<LPSTR>::Allocate(szaccount + szdomain);
  if (!group.name.IsValid()) {
    return LAST_WINAPI_ERROR;
  }

  // declare pointers to buffers for domain and account names (including
  // the terminating zero characters), both in the same continguous buffer
  LPSTR domainpart = group.name;
  LPSTR accountpart = domainpart + szdomain;
  // fill the buffers with the requested information; both domain
  // and account names are ended by zero characters; it divides them,
  // as long as they are needed separate
  if (LookupAccountSid(NULL, gsid, accountpart, &szaccount,
      domainpart, &szdomain, &sidtype) == FALSE) {
    return LAST_WINAPI_ERROR;
  }
  // we expect only SIDs representing a Windows group; not the others
  if (sidtype != SidTypeGroup && sidtype != SidTypeAlias &&
      sidtype != SidTypeLabel && sidtype != SidTypeWellKnownGroup) {
    return ERROR_BAD_ARGUMENTS;
  }

  // get the NetBIOS name of the current computer
  CHAR computer[MAX_COMPUTERNAME_LENGTH + 1];
  DWORD szcomputer = MAX_COMPUTERNAME_LENGTH + 1;
  if (GetComputerName(computer, &szcomputer) == FALSE) {
    return LAST_WINAPI_ERROR;
  }

  // if the group name is "<computer name>\None", it is actually the local
  // Users group reported this way, because the machine is in a domain;
  // replace the name with "<computer name>\Users" and update all related
  // variables: domainpart, szdomain, accountpart and szaccount
  if (stricmp(accountpart, "None") == 0 &&
      stricmp(domainpart, computer) == 0) {
      // the domain name is the NetBIOS name of this computer
      szdomain = szcomputer;
      // the account name is "Users"
      szaccount = 5;
      // allocate a new buffer with the size of the domain name,
      // the account name, backslash and the terminating zero character
      group.name = HeapMem<LPSTR>::Allocate(szdomain + szaccount + 2);
      // the domain part is the first in the group name
      domainpart = group.name;
      // the account part starts behind the terminating zero character
      // of the group name part, in the same continguous buffer
      accountpart = domainpart + szdomain + 1;
      // copy the computer name including the dividing zero character;
      // it will divide the domain and account names, as long as they
      // are needed separate
      CopyMemory(domainpart, computer, szcomputer + 1);
      // copy the member name including the terminating zero character
      CopyMemory(accountpart, "Users", 6);
  }

  // the domain part can be ampty, equal to "BUILTIN" or equal to this
  // computer name; in these cases, the group is a local one, otherwise
  // we need to know the domain controller to enquire about it;
  // store it in a UTF016 buffer for the enquiring API
  NetApiBuffer<LPWSTR> wdcname;
  LPWSTR wserver = NULL;
  if (szdomain > 0 && stricmp(domainpart, "BUILTIN") != 0 &&
      stricmp(domainpart, computer) != 0) {
    HeapMem<LPWSTR> wdomain = HeapStrUtf8ToWide(
      HeapBase::ProcessHeap(), domainpart);
    error = NetGetDCName(NULL, wdomain, (LPBYTE *) &wdcname);
    if (error == NERR_Success) {
      // the server name is returned prefixed by "\\", which is not
      // expected by the later used enquiring API
      wserver = wdcname + 2;
    } else if (error != NERR_DCNotFound) {
      return error;
    }
  }

  // convert the account name to UTF-16 for the enquiring API
  HeapMem<LPWSTR> waccount = HeapStrUtf8ToWide(
    HeapBase::ProcessHeap(), accountpart);
  if (!waccount.IsValid()) {
    return LAST_WINAPI_ERROR;
  }

  // from here on we won't need separate domain and account names;
  // replace the domain terminating zero character by backslash,
  // achieving the desired output: "<domain>\<account>"
  if (szdomain > 0) {
    domainpart[szdomain] = '\\';
  } else {
    MoveMemory(domainpart, accountpart, szaccount + 1);
  }

  // groups specified by complete SIDs can be domain SIDs and should
  // be enquired about by NetGroupGetUsers
  if (sidtype == SidTypeGroup || sidtype == SidTypeAlias) {
    if (wserver != NULL) {
      NetApiBuffer<PGROUP_USERS_INFO_0> users;
      DWORD read = 0, total = 0;
      error = NetGroupGetUsers(wserver, waccount, 0, (LPBYTE *) &users,
        MAX_PREFERRED_LENGTH, &read, &total, NULL);
      if (error == ERROR_ACCESS_DENIED) {
        // the current user may not have enough rights to enquire about
        // the group; it is not an error; the members will be empty
        return ERROR_SUCCESS;
      } else if (error != NERR_Success) {
        return error;
      }
      if (read > 0) {
        // copy the member names to UTF-8 strings; however, members 
        // from the same domain are returned without the "domain\"
        // prefix, so add it to have the consistent output
        group.members.Resize(read);
        for (DWORD i = 0; i < read; ++i) {
          HeapMem<LPSTR> member = HeapStrWideToUtf8(HeapBase::ProcessHeap(),
            users[i].grui0_name);
          // if the member name has the "domain\account" format, take it
          if (szdomain == 0 || strchr(member, '\\') != NULL) {
            group.members[i] = member;
          } else {
            // if the member name lacks the domain, allocate a new buffer
            // with the size of the domain name, the returned member name,
            // backslash and the terminating zero character
            size_t szmember = strlen(member);
            HeapMem<LPSTR> fullmember = HeapMem<LPSTR>::Allocate(
              szdomain + szmember + 2);
            if (!fullmember.IsValid()) {
              return LAST_WINAPI_ERROR;
            }
            // copy the domain name including the dividing backslash
            CopyMemory(fullmember, domainpart, szdomain + 1);
            // copy the member name including the terminating zero character
            CopyMemory(fullmember + szdomain + 1, member, szmember + 1);
            group.members[i] = fullmember;
          }
        }
      }
    // groups specified by short well-known SIDs ale local groups
    // and should be enquired about by NetLocalGroupGetUsers
    } else {
      NetApiBuffer<PLOCALGROUP_MEMBERS_INFO_3> members;
      DWORD read = 0, total = 0;
      error = NetLocalGroupGetMembers(wserver, waccount, 3, (LPBYTE *) &members,
        MAX_PREFERRED_LENGTH, &read, &total, NULL);
      if (error == ERROR_ACCESS_DENIED) {
        // the current user may not have enough rights to enquire about
        // the group; it is not an error; the members will be empty
        return ERROR_SUCCESS;
      } else if (error != NERR_Success) {
        return error;
      }
      if (read > 0) {
        // copy the member names to UTF-8 strings; they are in the format
        // "domain\account" returned by the NetLocalGroupGetMembers already
        group.members.Resize(read);
        for (DWORD i = 0; i < read; ++i) {
          group.members[i] = HeapStrWideToUtf8(HeapBase::ProcessHeap(),
            members[i].lgrmi3_domainandname);
        }
      }
    }
  }

  // groups do not have passwords on Windows; return the placeholder
  // character used on Linux when the password is not known
  group.passwd = HeapStrDup(HeapBase::ProcessHeap(), "x");

  return ERROR_SUCCESS;
}

// completes the user information using the uid (SID) member of it
static DWORD resolve_user(user_t & user, PSID usid) {
  assert(usid != NULL);

  // convert the input SID to string; although the string could be the
  // original input parameter, this will ensure always consistent ouptut
  if (ConvertSidToStringSid(usid, &user.uid) == FALSE) {
    return LAST_WINAPI_ERROR;
  }

  // get sizes of buffers to accomodate domain and account names
  DWORD szaccount = 0, szdomain = 0;
  SID_NAME_USE sidtype = SidTypeUnknown;
  if (LookupAccountSid(NULL, usid, NULL, &szaccount,
      NULL, &szdomain, &sidtype) != FALSE) {
    return ERROR_INVALID_FUNCTION;
  }
  DWORD error = LAST_WINAPI_ERROR;
  if (error != ERROR_INSUFFICIENT_BUFFER) {
    return error;
  }

  // allocate the buffer for both domain and account names; they
  // will be formatted "domain\account"
  user.name = HeapMem<LPSTR>::Allocate(szaccount + szdomain);
  if (!user.name.IsValid()) {
    return LAST_WINAPI_ERROR;
  }

  // declare pointers to buffers for domain and account names (including
  // the terminating zero characters), both in the same continguous buffer
  LPSTR domainpart = user.name;
  LPSTR accountpart = domainpart + szdomain;
  // fill the buffers with the requested information; both domain
  // and account names are ended by zero characters; it divides them,
  // as long as they are needed separate
  if (LookupAccountSid(NULL, usid, accountpart, &szaccount,
      domainpart, &szdomain, &sidtype) == FALSE) {
    return LAST_WINAPI_ERROR;
  }
  // we expect only SIDs representing a Windows user; not the others
  if (sidtype != SidTypeUser) {
    return ERROR_BAD_ARGUMENTS;
  }

  // get the NetBIOS name of the current computer
  CHAR computer[MAX_COMPUTERNAME_LENGTH + 1];
  DWORD szcomputer = MAX_COMPUTERNAME_LENGTH + 1;
  if (GetComputerName(computer, &szcomputer) == FALSE) {
    return LAST_WINAPI_ERROR;
  }

  // if the domain name is not this this computer name, it is a Windows
  // domain and we need to know the domain controller to enquire about
  // the specified user; store it in a UTF016 buffer for the enquiring API
  NetApiBuffer<LPWSTR> wdcname;
  LPWSTR wserver = NULL;
  if (stricmp(domainpart, computer) != 0) {
    HeapMem<LPWSTR> wdomain = HeapStrUtf8ToWide(
      HeapBase::ProcessHeap(), domainpart);
    error = NetGetDCName(NULL, wdomain, (LPBYTE *) &wdcname);
    if (error == NERR_Success) {
      // the server name is returned prefixed by "\\", which is not
      // expected by the later used enquiring API
      wserver = wdcname + 2;
    } else if (error != NERR_DCNotFound) {
      return error;
    }
  }

  // convert the account name to UTF-16 for the enquiring API
  HeapMem<LPWSTR> waccount = HeapStrUtf8ToWide(
    HeapBase::ProcessHeap(), accountpart);
  if (!waccount.IsValid()) {
    return LAST_WINAPI_ERROR;
  }

  // from here on we won't need separate domain and account names;
  // replace the domain terminating zero character by backslash,
  // achieving the desired output: "<domain>\<account>"
  if (szdomain > 0) {
    domainpart[szdomain] = '\\';
  } else {
    MoveMemory(domainpart, accountpart, szaccount + 1);
  }

  // get the user information from the computed server
  NetApiBuffer<PUSER_INFO_4> uinfo;
  error = NetUserGetInfo(wserver, waccount, 4, (LPBYTE *) &uinfo);
  if (error == ERROR_ACCESS_DENIED) {
    return ERROR_SUCCESS;
  } else if (error != NERR_Success) {
    return error;
  }

  // the primary group is returned as RID; either as alias or a group
  // (BUILTIN\Users or <domain>\Users, for example)
  SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
  Sid<> gsid;
  assert(DOMAIN_GROUP_RID_ADMINS < DOMAIN_ALIAS_RID_ADMINS);
  if (uinfo->usri4_primary_group_id >= DOMAIN_ALIAS_RID_ADMINS) {
    // convert the alias RID to a SID for a BUILTIN group
    if (AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
        uinfo->usri4_primary_group_id, 0, 0, 0, 0, 0, 0, &gsid) == FALSE) {
      return LAST_WINAPI_ERROR;
    }
  } else {
    // convert the group RID to a SID for the group starting with the same
    // sub-authorities as the user, just with the last one equal to the RID
    UCHAR count = *GetSidSubAuthorityCount(usid);
    if (AllocateAndInitializeSid(&authority, count, 0,
        0, 0, 0, 0, 0, 0, 0, &gsid) == FALSE) {
      return LAST_WINAPI_ERROR;
    }
    DWORD length = GetLengthSid(usid);
    if (CopySid(length, gsid, usid) == FALSE) {
      return LAST_WINAPI_ERROR;
    }
    ((SID *) gsid.Get())->SubAuthority[count - 1] = uinfo->usri4_primary_group_id;
  }

  // convert the primary group SID to string
  if (ConvertSidToStringSid(gsid, &user.gid) == FALSE) {
    return LAST_WINAPI_ERROR;
  }

  // if the password could not be read (because of lack of rights, e.g.),
  // groups do not have passwords on Windows; return the placeholder
  // character used on Linux when the password is not known
  user.passwd = HeapStrWideToUtf8(HeapBase::ProcessHeap(),
    uinfo->usri4_password != NULL ? uinfo->usri4_password : L"x");

  // populate the rest of user information
  user.gecos = HeapStrWideToUtf8(HeapBase::ProcessHeap(), uinfo->usri4_full_name);
  user.shell = HeapStrWideToUtf8(HeapBase::ProcessHeap(), uinfo->usri4_script_path);
  user.dir = HeapStrWideToUtf8(HeapBase::ProcessHeap(), uinfo->usri4_home_dir);

  return ERROR_SUCCESS;
}

// -----------------------------------------
// support for asynchronous method execution

// codes of exposed native methods
typedef enum {
  OPERATION_GETGRGID,
  OPERATION_GETGRNAM,
  OPERATION_GETPWNAM,
  OPERATION_GETPWUID
} operation_t;

// passes input/output parameters between the native method entry point
// and the worker method doing the work, which is called asynchronously
struct async_data_t {
  uv_work_t request;
  Persistent<Function> callback;
  DWORD error;

  operation_t operation;
  user_t user;
  group_t group;

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

// converts an object with the group information to the JavaScript result
static Local<Object> convert_group(group_t const & group) {
  Local<Object> result = Object::New();
  if (!result.IsEmpty()) {
    result->Set(name_symbol, String::New(group.name));
    // some parameters may be empty if the current user did not have
    // enough permissions to enquire about the group
    if (group.passwd.IsValid()) {
      result->Set(passwd_symbol, String::New(group.passwd));
    }
    result->Set(gid_symbol, String::New(group.gid));
    if (group.members.IsValid()) {
      Local<Array> members = Array::New(group.members.Size());
      if (!members.IsEmpty()) {
        for (int i = 0; i < group.members.Size(); ++i) {
          members->Set(i, String::New(group.members[i]));
        }
      }
      result->Set(members_symbol, members);
    } else {
      result->Set(members_symbol, Array::New(0));
    }
  }
  return result;
}

// converts an object with the user information to the JavaScript result
static Local<Object> convert_user(user_t const & user) {
  Local<Object> result = Object::New();
  if (!result.IsEmpty()) {
    result->Set(name_symbol, String::New(user.name));
    // some parameters may be empty if the current user did not have
    // enough permissions to enquire about the user
    if (user.passwd.IsValid()) {
      result->Set(passwd_symbol, String::New(user.passwd));
    }
    result->Set(uid_symbol, String::New(user.uid));
    if (user.gid.IsValid()) {
      result->Set(gid_symbol, String::New(user.gid));
    }
    if (user.gecos.IsValid()) {
      result->Set(gecos_symbol, String::New(user.gecos));
    }
    if (user.shell.IsValid()) {
      result->Set(shell_symbol, String::New(user.shell));
    }
    if (user.dir.IsValid()) {
      result->Set(dir_symbol, String::New(user.dir));
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
  if (async_data->error == ERROR_NONE_MAPPED) {
    // unknown user/group name/SID is no error; return undefined
    argv[0] = Local<Value>::New(Null());
    argv[1] = Local<Value>::New(Undefined());
    argc = 2;
  } else if (async_data->error != ERROR_SUCCESS) {
    argv[0] = WinapiErrnoException(async_data->error);
  } else {
    // in case of success, make the first argument (error) null
    argv[0] = Local<Value>::New(Null());
    // in case of success, populate the second and other arguments
    switch (async_data->operation) {
      case OPERATION_GETGRGID:
      case OPERATION_GETGRNAM: {
        argv[1] = convert_group(async_data->group);
        argc = 2;
        break;
      }
      case OPERATION_GETPWNAM:
      case OPERATION_GETPWUID: {
        argv[1] = convert_user(async_data->user);
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

// --------------------------------------------------
// getgrgid - gets group information for a group SID:
// { name, passwd, gid, members }  getgrgid( gid, [callback] )

// completes the group information using the gid (string) member of it
static DWORD getgrgid_sync(group_t & group) {
  LocalMem<PSID> gsid;
  if (ConvertStringSidToSid(group.gid, &gsid) == FALSE) {
    return LAST_WINAPI_ERROR;
  }

  group.gid.Dispose();

  return resolve_group(group, gsid);
}

// passes the execution to getgrgid_sync; the results will be processed
// by after_async
static void getgrgid_async(uv_work_t * req) {
  assert(req != NULL);
  async_data_t * async_data = static_cast<async_data_t *>(req->data);
  async_data->error = getgrgid_sync(async_data->group);
}

// the native entry point for the exposed getgrgid function
static Handle<Value> getgrgid_impl(Arguments const & args) {
  HandleScope scope;

  int argc = args.Length();
  if (argc < 1)
    return THROW_TYPE_ERROR("gid required");
  if (argc > 2)
    return THROW_TYPE_ERROR("too many arguments");
  if (!args[0]->IsString())
    return THROW_TYPE_ERROR("gid must be a string");
  if (argc > 1 && !args[1]->IsFunction())
    return THROW_TYPE_ERROR("callback must be a function");

  String::AsciiValue gid(args[0]->ToString());

  // if no callback was provided, assume the synchronous scenario,
  // call the method_sync immediately and return its results
  if (!args[1]->IsFunction()) {
    group_t group;
    group.gid = LocalStrDup(*gid);
    if (!group.gid.IsValid()) {
      return THROW_LAST_WINAPI_ERROR;
    }
    DWORD error = getgrgid_sync(group);
    if (error == ERROR_NONE_MAPPED) {
      return Undefined();
    }
    if (error != ERROR_SUCCESS) {
      return THROW_WINAPI_ERROR(error);
    }
    Local<Object> result = convert_group(group);
    return scope.Close(result);
  }

  // prepare parameters for the method_sync to be called later
  // from the method_async called from the worker thread
  CppObj<async_data_t *> async_data = new async_data_t(
    Local<Function>::Cast(args[1]));
  async_data->operation = OPERATION_GETPWUID;
  async_data->group.gid = LocalStrDup(*gid);
  if (!async_data->user.uid.IsValid()) {
    return THROW_LAST_WINAPI_ERROR;
  }

  // queue the method_async to be called when posibble and
  // after_async to send its result to the external callback
  uv_queue_work(uv_default_loop(), &async_data->request,
    getgrgid_async, (uv_after_work_cb) after_async);

  async_data.Detach();
  return Undefined();
}

// ----------------------------------------------------
// getgrnam - gets group information for a group name:
// { name, passwd, gid, members }  getgrnam( name, [callback] )

// completes the group information using the name member of it
static DWORD getgrnam_sync(group_t & group) {
  HeapMem<PSID> gsid;
  DWORD error = resolve_name(group.name, &gsid);
  if (error != ERROR_SUCCESS) {
    return error;
  }
  return resolve_group(group, gsid);
}

// passes the execution to getgrnam_sync; the results will be processed
// by after_async
static void getgrnam_async(uv_work_t * req) {
  assert(req != NULL);
  async_data_t * async_data = static_cast<async_data_t *>(req->data);
  async_data->error = getgrnam_sync(async_data->group);
}

// the native entry point for the exposed getgrnam function
static Handle<Value> getgrnam_impl(Arguments const & args) {
  HandleScope scope;

  int argc = args.Length();
  if (argc < 1)
    return THROW_TYPE_ERROR("name required");
  if (argc > 2)
    return THROW_TYPE_ERROR("too many arguments");
  if (!args[0]->IsString())
    return THROW_TYPE_ERROR("name must be a string");
  if (argc > 1 && !args[1]->IsFunction())
    return THROW_TYPE_ERROR("callback must be a function");

  String::Utf8Value name(args[0]->ToString());

  // if no callback was provided, assume the synchronous scenario,
  // call the method_sync immediately and return its results
  if (!args[1]->IsFunction()) {
    group_t group;
    group.name = HeapStrDup(HeapBase::ProcessHeap(), *name);
    if (!group.name.IsValid()) {
      return THROW_LAST_WINAPI_ERROR;
    }
    DWORD error = getgrnam_sync(group);
    if (error == ERROR_NONE_MAPPED) {
      return Undefined();
    }
    if (error != ERROR_SUCCESS) {
      return THROW_WINAPI_ERROR(error);
    }
    Local<Object> result = convert_group(group);
    return scope.Close(result);
  }

  // prepare parameters for the method_sync to be called later
  // from the method_async called from the worker thread
  CppObj<async_data_t *> async_data = new async_data_t(
    Local<Function>::Cast(args[1]));
  async_data->operation = OPERATION_GETGRNAM;
  async_data->group.name = HeapStrDup(HeapBase::ProcessHeap(), *name);
  if (!async_data->group.name.IsValid()) {
    return THROW_LAST_WINAPI_ERROR;
  }

  // queue the method_async to be called when posibble and
  // after_async to send its result to the external callback
  uv_queue_work(uv_default_loop(), &async_data->request,
    getgrnam_async, (uv_after_work_cb) after_async);

  async_data.Detach();
  return Undefined();
}

// -------------------------------------------------
// getpwnam - gets user information for a user name:
// { name, passwd, uid, gid, gecos, shell, dir }  getpwnam( name, [callback] )

// completes the user information using the name member of it
static DWORD getpwnam_sync(user_t & user) {
  HeapMem<PSID> usid;
  DWORD error = resolve_name(user.name, &usid);
  if (error != ERROR_SUCCESS) {
    return error;
  }
  return resolve_user(user, usid);
}

// passes the execution to getpwnam_sync; the results will be processed
// by after_async
static void getpwnam_async(uv_work_t * req) {
  assert(req != NULL);
  async_data_t * async_data = static_cast<async_data_t *>(req->data);
  async_data->error = getpwnam_sync(async_data->user);
}

// the native entry point for the exposed getpwnam function
static Handle<Value> getpwnam_impl(Arguments const & args) {
  HandleScope scope;

  int argc = args.Length();
  if (argc < 1)
    return THROW_TYPE_ERROR("name required");
  if (argc > 2)
    return THROW_TYPE_ERROR("too many arguments");
  if (!args[0]->IsString())
    return THROW_TYPE_ERROR("name must be a string");
  if (argc > 1 && !args[1]->IsFunction())
    return THROW_TYPE_ERROR("callback must be a function");

  String::Utf8Value name(args[0]->ToString());

  // if no callback was provided, assume the synchronous scenario,
  // call the method_sync immediately and return its results
  if (!args[1]->IsFunction()) {
    user_t user;
    user.name = HeapStrDup(HeapBase::ProcessHeap(), *name);
    if (!user.name.IsValid()) {
      return THROW_LAST_WINAPI_ERROR;
    }
    DWORD error = getpwnam_sync(user);
    if (error == ERROR_NONE_MAPPED) {
      return Undefined();
    }
    if (error != ERROR_SUCCESS) {
      return THROW_WINAPI_ERROR(error);
    }
    Local<Object> result = convert_user(user);
    return scope.Close(result);
  }

  // prepare parameters for the method_sync to be called later
  // from the method_async called from the worker thread
  CppObj<async_data_t *> async_data = new async_data_t(
    Local<Function>::Cast(args[1]));
  async_data->operation = OPERATION_GETPWNAM;
  async_data->user.name = HeapStrDup(HeapBase::ProcessHeap(), *name);
  if (!async_data->user.name.IsValid()) {
    return THROW_LAST_WINAPI_ERROR;
  }

  // queue the method_async to be called when posibble and
  // after_async to send its result to the external callback
  uv_queue_work(uv_default_loop(), &async_data->request,
    getpwnam_async, (uv_after_work_cb) after_async);

  async_data.Detach();
  return Undefined();
}

// ------------------------------------------------
// getpwuid - gets user information for a user SID:
// { name, passwd, uid, gid, gecos, shell, dir }  getpwuid( uid, [callback] )

// completes the user information using the uid (string) member of it
static DWORD getpwuid_sync(user_t & user) {
  LocalMem<PSID> usid;
  if (ConvertStringSidToSid(user.uid, &usid) == FALSE) {
    return LAST_WINAPI_ERROR;
  }

  user.uid.Dispose();

  return resolve_user(user, usid);
}

// passes the execution to getpwuid_sync; the results will be processed
// by after_async
static void getpwuid_async(uv_work_t * req) {
  assert(req != NULL);
  async_data_t * async_data = static_cast<async_data_t *>(req->data);
  async_data->error = getpwuid_sync(async_data->user);
}

// the native entry point for the exposed getpwuid function
static Handle<Value> getpwuid_impl(Arguments const & args) {
  HandleScope scope;

  int argc = args.Length();
  if (argc < 1)
    return THROW_TYPE_ERROR("uid required");
  if (argc > 2)
    return THROW_TYPE_ERROR("too many arguments");
  if (!args[0]->IsString())
    return THROW_TYPE_ERROR("uid must be a string");
  if (argc > 1 && !args[1]->IsFunction())
    return THROW_TYPE_ERROR("callback must be a function");

  String::AsciiValue uid(args[0]->ToString());

  // if no callback was provided, assume the synchronous scenario,
  // call the method_sync immediately and return its results
  if (!args[1]->IsFunction()) {
    user_t user;
    user.uid = LocalStrDup(*uid);
    if (!user.uid.IsValid()) {
      return THROW_LAST_WINAPI_ERROR;
    }
    DWORD error = getpwuid_sync(user);
    if (error == ERROR_NONE_MAPPED) {
      return Undefined();
    }
    if (error != ERROR_SUCCESS) {
      return THROW_WINAPI_ERROR(error);
    }
    Local<Object> result = convert_user(user);
    return scope.Close(result);
  }

  // prepare parameters for the method_sync to be called later
  // from the method_async called from the worker thread
  CppObj<async_data_t *> async_data = new async_data_t(
    Local<Function>::Cast(args[1]));
  async_data->operation = OPERATION_GETPWUID;
  async_data->user.uid = LocalStrDup(*uid);
  if (!async_data->user.uid.IsValid()) {
    return THROW_LAST_WINAPI_ERROR;
  }

  // queue the method_async to be called when posibble and
  // after_async to send its result to the external callback
  uv_queue_work(uv_default_loop(), &async_data->request,
    getpwuid_async, (uv_after_work_cb) after_async);

  async_data.Detach();
  return Undefined();
}

// exposes methods implemented by this sub-package and initializes the
// string symbols for the converted resulting object literals; to be
// called from the add-on module-initializing function
void init(Handle<Object> target) {
  HandleScope scope;

  NODE_SET_METHOD(target, "getgrgid", getgrgid_impl);
  NODE_SET_METHOD(target, "getgrnam", getgrnam_impl);
  NODE_SET_METHOD(target, "getpwnam", getpwnam_impl);
  NODE_SET_METHOD(target, "getpwuid", getpwuid_impl);

  name_symbol = NODE_PSYMBOL("name");
  passwd_symbol = NODE_PSYMBOL("passwd");
  uid_symbol = NODE_PSYMBOL("uid");
  gid_symbol = NODE_PSYMBOL("gid");
  gecos_symbol = NODE_PSYMBOL("gecos");
  shell_symbol = NODE_PSYMBOL("shell");
  dir_symbol = NODE_PSYMBOL("dir");
  members_symbol = NODE_PSYMBOL("members");
}

} // namespace posix_win

#endif // _WIN32
