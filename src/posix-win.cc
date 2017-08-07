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

namespace posix_win {

using node::WinapiErrnoException;
using v8::Local;
using v8::Function;
using v8::Object;
using v8::Array;
using v8::Value;
using v8::String;
using Nan::FunctionCallbackInfo;
using Nan::GetCurrentContext;
using Nan::AsyncQueueWorker;
using Nan::AsyncWorker;
using Nan::Callback;
using Nan::HandleScope;
using Nan::ThrowError;
using Nan::ThrowTypeError;
using Nan::New;
using Nan::Null;
using Nan::Undefined;
using Nan::Get;
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
// to its SID; the id parameter must be freed by LocalFree when not needed
static DWORD resolve_name(LPCSTR name, PSID * id) {
  assert(name != NULL);
  assert(id != NULL);

  // convert UTF-8 source to UTF-16 to be able to use wide-character
  // Win32 API, which can handle names with any characters inside
  HeapMem<LPWSTR> wname = HeapStrUtf8ToWide(HeapBase::ProcessHeap(), name);
  if (!wname.IsValid()) {
    return GetLastError();
  }

  // get sizes of buffers to accomodate the domain name and SID
  DWORD szsid = 0, szdomain = 0;
  SID_NAME_USE sidtype = SidTypeUnknown;
  if (LookupAccountNameW(NULL, wname, NULL, &szsid,
      NULL, &szdomain, &sidtype) != FALSE) {
    return ERROR_INVALID_FUNCTION;
  }
  DWORD error = GetLastError();
  if (error != ERROR_INSUFFICIENT_BUFFER) {
    return error;
  }

  // allocate the buffer for SID
  HeapMem<PSID> sid = HeapMem<PSID>::Allocate(szsid);
  if (!sid.IsValid()) {
    return GetLastError();
  }
  // allocate the buffer for the source domain
  HeapMem<LPWSTR> domain = HeapMem<LPWSTR>::Allocate(
    szdomain * sizeof(WCHAR));
  if (!domain.IsValid()) {
    return GetLastError();
  }

  // get the SID and the source domain name; the latter is not needed
  // but it is always returned
  if (LookupAccountNameW(NULL, wname, sid, &szsid,
      domain, &szdomain, &sidtype) == FALSE) {
    return GetLastError();
  }

  *id = sid.Detach();

  return ERROR_SUCCESS;
}

// completes the group information using the gid (SID) member of it
static DWORD resolve_group(group_t & group, PSID gsid,
                           bool populateGroupMembers) {
  assert(gsid != NULL);

  // convert the input SID to string; although the string could be the
  // original input parameter, this will ensure always consistent ouptut
  if (ConvertSidToStringSid(gsid, &group.gid) == FALSE) {
    return GetLastError();
  }

  // get sizes of buffers to accomodate domain and account names
  DWORD szaccount = 0, szdomain = 0;
  SID_NAME_USE sidtype = SidTypeUnknown;
  if (LookupAccountSidW(NULL, gsid, NULL, &szaccount,
      NULL, &szdomain, &sidtype) != FALSE) {
    return ERROR_INVALID_FUNCTION;
  }
  DWORD error = GetLastError();
  if (error != ERROR_INSUFFICIENT_BUFFER) {
    return error;
  }

  // allocate the buffer for both domain and account names; they
  // will be formatted "domain\account"
  HeapMem<LPWSTR> name = HeapMem<LPWSTR>::Allocate(
    (szdomain + szaccount) * sizeof(WCHAR));
  if (!name.IsValid()) {
    return GetLastError();
  }

  // declare pointers to buffers for domain and account names (including
  // the terminating zero characters), both in the same continguous buffer
  LPWSTR domainpart = name;
  LPWSTR accountpart = domainpart + szdomain;

  // fill the buffers with the requested information; both domain
  // and account names are ended by zero characters; it divides them,
  // as long as they are needed separate
  if (LookupAccountSidW(NULL, gsid, accountpart, &szaccount,
      domainpart, &szdomain, &sidtype) == FALSE) {
    return GetLastError();
  }
  // we expect only SIDs representing a Windows group; not the others
  if (sidtype != SidTypeGroup && sidtype != SidTypeAlias &&
      sidtype != SidTypeLabel && sidtype != SidTypeWellKnownGroup) {
    return ERROR_BAD_ARGUMENTS;
  }

  // get the NetBIOS name of the current computer
  WCHAR computer[MAX_COMPUTERNAME_LENGTH + 1];
  DWORD szcomputer = MAX_COMPUTERNAME_LENGTH + 1;
  if (GetComputerNameW(computer, &szcomputer) == FALSE) {
    return GetLastError();
  }

  // if the group name is "<computer name>\None", it is actually the local
  // Users group reported this way, because the machine is in a domain;
  // replace the name with "<computer name>\Users" and update all related
  // variables: domainpart, szdomain, accountpart and szaccount
  if (_wcsicmp(accountpart, L"None") == 0 &&
      _wcsicmp(domainpart, computer) == 0) {
      // the domain name is the NetBIOS name of this computer
      szdomain = szcomputer;
      // the account name is "Users"
      szaccount = 5;
      // allocate a new buffer with the size of the domain name,
      // the account name, backslash and the terminating zero character
      name = HeapMem<LPWSTR>::Allocate(
        (szdomain + szaccount + 2) * sizeof(WCHAR));
      if (!name.IsValid()) {
        return GetLastError();
      }
      // the domain part is the first in the group name
      domainpart = name;
      // the account part starts behind the terminating zero character
      // of the group name part, in the same continguous buffer
      accountpart = domainpart + szdomain + 1;
      // copy the computer name including the dividing zero character;
      // it will divide the domain and account names, as long as they
      // are needed separate
      CopyMemory(domainpart, computer, (szcomputer + 1) * sizeof(WCHAR));
      // copy the member name including the terminating zero character
      CopyMemory(accountpart, L"Users", 6 * sizeof(WCHAR));
  }

  // groups specified by complete SIDs can be domain SIDs and should
  // be enquired about by NetGroupGetUsers
  if (populateGroupMembers &&
      (sidtype == SidTypeGroup || sidtype == SidTypeAlias)) {
    // the domain part can be ampty, equal to "BUILTIN" or equal to this
    // computer name; in these cases, the group is a local one, otherwise
    // we need to know the domain controller to enquire about it;
    // store it in a UTF-16 buffer for the enquiring API
    NetApiBuffer<LPWSTR> wdcname;
    LPWSTR wserver = NULL;
    if (szdomain > 0 && _wcsicmp(domainpart, L"BUILTIN") != 0 &&
        _wcsicmp(domainpart, computer) != 0) {
      error = NetGetDCName(NULL, domainpart, (LPBYTE *) &wdcname);
      if (error == NERR_Success) {
        // the server name is returned prefixed by "\\", which is not
        // expected by the later used enquiring API
        wserver = wdcname + 2;
      } else if (error != NERR_DCNotFound) {
        return error;
      }
    }
    if (wserver != NULL) {
      NetApiBuffer<PGROUP_USERS_INFO_0> users;
      DWORD read = 0, total = 0;
      error = NetGroupGetUsers(wserver, accountpart, 0, (LPBYTE *) &users,
        MAX_PREFERRED_LENGTH, &read, &total, NULL);
      if (error == ERROR_ACCESS_DENIED) {
        // the current user may not have enough rights to enquire about
        // the group; it is not an error; the members will be empty
        return ERROR_SUCCESS;
      } else if (error != NERR_Success) {
        return error;
      }
      if (read > 0) {
        // convert the domain part to UTF-8 to be able to prepend it
        // to member names computed below
        HeapMem<LPSTR> domain = HeapStrWideToUtf8(
          HeapBase::ProcessHeap(), domainpart);
        if (!domain.IsValid()) {
          return GetLastError();
        }
        int domainlen = strlen(domain);
        // copy the member names to UTF-8 strings; however, members 
        // from the same domain are returned without the "domain\"
        // prefix, so add it to have the consistent output
        group.members.Resize(read);
        for (DWORD i = 0; i < read; ++i) {
          HeapMem<LPSTR> member = HeapStrWideToUtf8(HeapBase::ProcessHeap(),
            users[i].grui0_name);
          if (!member.IsValid()) {
            return GetLastError();
          }
          // if the member name has the "domain\account" format, take it
          if (szdomain == 0 || strchr(member, '\\') != NULL) {
            group.members[i] = member;
          } else {
            // if the member name lacks the domain, allocate a new buffer
            // with the size of the domain name, the returned member name,
            // backslash and the terminating zero character
            size_t szmember = strlen(member);
            HeapMem<LPSTR> fullmember = HeapMem<LPSTR>::Allocate(
              domainlen + szmember + 2);
            if (!fullmember.IsValid()) {
              return GetLastError();
            }
            // copy the domain name including the dividing backslash
            CopyMemory(fullmember, domain, domainlen);
            fullmember[domainlen] = '\\';
            // copy the member name including the terminating zero character
            CopyMemory(fullmember + domainlen + 1, member, szmember + 1);
            group.members[i] = fullmember;
          }
        }
      }
    // groups specified by short well-known SIDs ale local groups
    // and should be enquired about by NetLocalGroupGetUsers
    } else {
      NetApiBuffer<PLOCALGROUP_MEMBERS_INFO_3> members;
      DWORD read = 0, total = 0;
      error = NetLocalGroupGetMembers(wserver, accountpart, 3,
        (LPBYTE *) &members, MAX_PREFERRED_LENGTH, &read, &total, NULL);
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
          if (!group.members[i].IsValid()) {
            return GetLastError();
          }
        }
      }
    }
  }

  // from here on we won't need separate domain and account names;
  // replace the domain terminating zero character by backslash,
  // achieving the desired output: "<domain>\<account>"
  if (szdomain > 0) {
    domainpart[szdomain] = L'\\';
  } else {
    MoveMemory(domainpart, accountpart, (szaccount + 1) * sizeof(WCHAR));
  }

  // allocate the buffer for both domain and account names; they
  // will be formatted "domain\account"
  group.name = HeapStrWideToUtf8(HeapBase::ProcessHeap(), name);
  if (!group.name.IsValid()) {
    return GetLastError();
  }

  // groups do not have passwords on Windows; return the placeholder
  // character used on Linux when the password is not known
  group.passwd = HeapStrDup(HeapBase::ProcessHeap(), "x");
  if (!group.passwd.IsValid()) {
    return GetLastError();
  }

  return ERROR_SUCCESS;
}

// completes the user information using the uid (SID) member of it
static DWORD resolve_user(user_t & user, PSID usid) {
  assert(usid != NULL);

  // convert the input SID to string; although the string could be the
  // original input parameter, this will ensure always consistent ouptut
  if (ConvertSidToStringSid(usid, &user.uid) == FALSE) {
    return GetLastError();
  }

  // get sizes of buffers to accomodate domain and account names
  DWORD szaccount = 0, szdomain = 0;
  SID_NAME_USE sidtype = SidTypeUnknown;
  if (LookupAccountSidW(NULL, usid, NULL, &szaccount,
      NULL, &szdomain, &sidtype) != FALSE) {
    return ERROR_INVALID_FUNCTION;
  }
  DWORD error = GetLastError();
  if (error != ERROR_INSUFFICIENT_BUFFER) {
    return error;
  }

  // allocate the buffer for both domain and account names; they
  // will be formatted "domain\account"
  HeapMem<LPWSTR> name = HeapMem<LPWSTR>::Allocate(
    (szdomain + szaccount) * sizeof(WCHAR));
  if (!name.IsValid()) {
    return GetLastError();
  }

  // declare pointers to buffers for domain and account names (including
  // the terminating zero characters), both in the same continguous buffer
  LPWSTR domainpart = name;
  LPWSTR accountpart = domainpart + szdomain;

  // fill the buffers with the requested information; both domain
  // and account names are ended by zero characters; it divides them,
  // as long as they are needed separate
  if (LookupAccountSidW(NULL, usid, accountpart, &szaccount,
      domainpart, &szdomain, &sidtype) == FALSE) {
    return GetLastError();
  }
  // we expect only SIDs representing a Windows user; not the others
  if (sidtype != SidTypeUser) {
    return ERROR_BAD_ARGUMENTS;
  }

  // get the NetBIOS name of the current computer
  WCHAR computer[MAX_COMPUTERNAME_LENGTH + 1];
  DWORD szcomputer = MAX_COMPUTERNAME_LENGTH + 1;
  if (GetComputerNameW(computer, &szcomputer) == FALSE) {
    return GetLastError();
  }

  // if the domain name is not this this computer name, it is a Windows
  // domain and we need to know the domain controller to enquire about
  // the specified user; store it in a UTF-16 buffer for the enquiring API
  NetApiBuffer<LPWSTR> wdcname;
  LPWSTR wserver = NULL;
  if (_wcsicmp(domainpart, computer) != 0) {
    error = NetGetDCName(NULL, domainpart, (LPBYTE *) &wdcname);
    if (error == NERR_Success) {
      // the server name is returned prefixed by "\\", which is not
      // expected by the later used enquiring API
      wserver = wdcname + 2;
    } else if (error != NERR_DCNotFound) {
      return error;
    }
  }

  // get the user information from the computed server
  NetApiBuffer<PUSER_INFO_4> uinfo;
  error = NetUserGetInfo(wserver, accountpart, 4, (LPBYTE *) &uinfo);
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
      return GetLastError();
    }
  } else {
    // convert the group RID to a SID for the group starting with the same
    // sub-authorities as the user, just with the last one equal to the RID
    UCHAR count = *GetSidSubAuthorityCount(usid);
    if (AllocateAndInitializeSid(&authority, count, 0,
        0, 0, 0, 0, 0, 0, 0, &gsid) == FALSE) {
      return GetLastError();
    }
    DWORD length = GetLengthSid(usid);
    if (CopySid(length, gsid, usid) == FALSE) {
      return GetLastError();
    }
    ((SID *) gsid.Get())->SubAuthority[count - 1] = uinfo->usri4_primary_group_id;
  }

  // from here on we won't need separate domain and account names;
  // replace the domain terminating zero character by backslash,
  // achieving the desired output: "<domain>\<account>"
  if (szdomain > 0) {
    domainpart[szdomain] = L'\\';
  } else {
    MoveMemory(domainpart, accountpart, (szaccount + 1) * sizeof(WCHAR));
  }

  // allocate the buffer for both domain and account names; they
  // will be formatted "domain\account"
  user.name = HeapStrWideToUtf8(HeapBase::ProcessHeap(), name);
  if (!user.name.IsValid()) {
    return GetLastError();
  }

  // convert the primary group SID to string
  if (ConvertSidToStringSid(gsid, &user.gid) == FALSE) {
    return GetLastError();
  }

  // if the password could not be read (because of lack of rights, e.g.),
  // groups do not have passwords on Windows; return the placeholder
  // character used on Linux when the password is not known
  user.passwd = HeapStrWideToUtf8(HeapBase::ProcessHeap(),
    uinfo->usri4_password != NULL ? uinfo->usri4_password : L"x");
  if (!user.passwd.IsValid()) {
    return GetLastError();
  }

  // populate the rest of user information
  user.gecos = HeapStrWideToUtf8(HeapBase::ProcessHeap(), uinfo->usri4_full_name);
  if (!user.gecos.IsValid()) {
    return GetLastError();
  }
  user.shell = HeapStrWideToUtf8(HeapBase::ProcessHeap(), uinfo->usri4_script_path);
  if (!user.shell.IsValid()) {
    return GetLastError();
  }
  user.dir = HeapStrWideToUtf8(HeapBase::ProcessHeap(), uinfo->usri4_home_dir);
  if (!user.dir.IsValid()) {
    return GetLastError();
  }

  return ERROR_SUCCESS;
}

// converts an object with the group information to the JavaScript result
static Local<Value> convert_group(group_t const & group) {
  Local<Object> result = New<Object>();
  if (!result.IsEmpty()) {
    Set(result, New<String>("name").ToLocalChecked(),
      New<String>((LPSTR) group.name).ToLocalChecked());
    // some parameters may be empty if the current user did not have
    // enough permissions to enquire about the group
    if (group.passwd.IsValid()) {
      Set(result, New<String>("passwd").ToLocalChecked(),
        New<String>((LPSTR) group.passwd).ToLocalChecked());
    }
    Set(result, New<String>("gid").ToLocalChecked(),
      New<String>((LPSTR) group.gid).ToLocalChecked());
    if (group.members.IsValid()) {
      Local<Array> members = New<Array>(group.members.Size());
      if (!members.IsEmpty()) {
        for (int i = 0; i < group.members.Size(); ++i) {
          Set(members, i,
            New<String>((LPSTR) group.members[i]).ToLocalChecked());
        }
      }
      Set(result, New<String>("members").ToLocalChecked(), members);
    } else {
      Set(result, New<String>("members").ToLocalChecked(), New<Array>(0));
    }
  }
  return result;
}

// converts an object with the user information to the JavaScript result
static Local<Value> convert_user(user_t const & user) {
  Local<Object> result = New<Object>();
  if (!result.IsEmpty()) {
    Set(result, New<String>("name").ToLocalChecked(),
      New<String>((LPSTR) user.name).ToLocalChecked());
    // some parameters may be empty if the current user did not have
    // enough permissions to enquire about the user
    if (user.passwd.IsValid()) {
      Set(result, New<String>("passwd").ToLocalChecked(),
        New<String>((LPSTR) user.passwd).ToLocalChecked());
    }
    Set(result, New<String>("uid").ToLocalChecked(),
      New<String>((LPSTR) user.uid).ToLocalChecked());
    if (user.gid.IsValid()) {
      Set(result, New<String>("gid").ToLocalChecked(),
        New<String>((LPSTR) user.gid).ToLocalChecked());
    }
    if (user.gecos.IsValid()) {
      Set(result, New<String>("gecos").ToLocalChecked(),
        New<String>((LPSTR) user.gecos).ToLocalChecked());
    }
    if (user.shell.IsValid()) {
      Set(result, New<String>("shell").ToLocalChecked(),
        New<String>((LPSTR) user.shell).ToLocalChecked());
    }
    if (user.dir.IsValid()) {
      Set(result, New<String>("dir").ToLocalChecked(),
        New<String>((LPSTR) user.dir).ToLocalChecked());
    }
  }
  return result;
}

static bool shall_populate_group_members(
    FunctionCallbackInfo<Value> const & info) {
  HandleScope scope;
  Local<Value> posix = info.Holder();
  assert(posix->IsObject());
  Local<Value> options = Get(posix->ToObject(),
    New<String>("options").ToLocalChecked())
    .ToLocalChecked()->ToObject();
  assert(options->IsObject());
  return Get(options->ToObject(),
    New<String>("populateGroupMembers").ToLocalChecked())
    .ToLocalChecked()->BooleanValue();
}

// --------------------------------------------------
// getgrgid - gets group information for a group SID:
// { name, passwd, gid, members }  getgrgid( gid, [callback] )

// completes the group information using the gid (string) member of it
static DWORD getgrgid_impl(group_t & group, bool populateGroupMembers) {
  LocalMem<PSID> gsid;
  if (ConvertStringSidToSid(group.gid, &gsid) == FALSE) {
    return GetLastError();
  }

  group.gid.Dispose();

  return resolve_group(group, gsid, populateGroupMembers);
}

// passes input/output parameters between the native method entry point
// and the worker method doing the work, which is called asynchronously
class getgrgid_worker : public AsyncWorker {
  public:
    getgrgid_worker(Callback * callback, LPSTR gid, bool populateGroupMembers)
    : AsyncWorker(callback), populateGroupMembers(populateGroupMembers) {
      group.gid = LocalStrDup(gid);
      error = group.gid.IsValid() ? ERROR_SUCCESS : GetLastError();
    }

    ~getgrgid_worker() {}

  // passes the execution to getgrgid_impl
  void Execute() {
    if (error == ERROR_SUCCESS) {
      error = getgrgid_impl(group, populateGroupMembers);
    }
  }

  // called after an asynchronously called method (method_impl) has
  // finished to convert the results to JavaScript objects and pass
  // them to JavaScript callback
  void HandleOKCallback() {
    HandleScope scope;
    if (error == ERROR_NONE_MAPPED) {
      // pass the results to the external callback
      Local<Value> argv[] = {
        // in case of success, make the first argument (error) null
        Null(),
        // in case of success, populate the second and other arguments
        Undefined()
      };
      callback->Call(2, argv);
    } else if (error != ERROR_SUCCESS) {
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
        convert_group(group)
      };
      callback->Call(2, argv);
    }
  }

  private:
    bool populateGroupMembers;
    DWORD error;
    group_t group;
};

// the native entry point for the exposed getgrgid function
NAN_METHOD(getgrgid) {
  int argc = info.Length();
  if (argc < 1)
    return ThrowTypeError("gid required");
  if (argc > 2)
    return ThrowTypeError("too many arguments");
  if (!info[0]->IsString())
    return ThrowTypeError("gid must be a string");
  if (argc > 1 && !info[1]->IsFunction())
    return ThrowTypeError("callback must be a function");

  String::Utf8Value gid(info[0]->ToString());
  bool populateGroupMembers = shall_populate_group_members(info);

  // if no callback was provided, assume the synchronous scenario,
  // call the method_sync immediately and return its results
  if (!info[1]->IsFunction()) {
    HandleScope scope;
    group_t group;
    group.gid = LocalStrDup(*gid);
    if (!group.gid.IsValid())
      return ThrowLastWinapiError();
    DWORD error = getgrgid_impl(group, populateGroupMembers);
    if (error == ERROR_NONE_MAPPED)
      return info.GetReturnValue().Set(Undefined());
    if (error != ERROR_SUCCESS)
      return ThrowWinapiError(error);
    return info.GetReturnValue().Set(convert_group(group));
  }

  // prepare parameters for the method_impl to be called later;
  // queue the worker to be called when posibble and send its
  // result to the external callback
  Callback * callback = new Callback(info[1].As<Function>());
  AsyncQueueWorker(new getgrgid_worker(callback, *gid,
    populateGroupMembers));
}

// ----------------------------------------------------
// getgrnam - gets group information for a group name:
// { name, passwd, gid, members }  getgrnam( name, [callback] )

// completes the group information using the name member of it
static DWORD getgrnam_impl(group_t & group, bool populateGroupMembers) {
  HeapMem<PSID> gsid;
  DWORD error = resolve_name(group.name, &gsid);
  if (error != ERROR_SUCCESS) {
    return error;
  }
  return resolve_group(group, gsid, populateGroupMembers);
}

// passes input/output parameters between the native method entry point
// and the worker method doing the work, which is called asynchronously
class getgrnam_worker : public AsyncWorker {
  public:
    getgrnam_worker(Callback * callback, LPSTR name, bool populateGroupMembers)
    : AsyncWorker(callback), populateGroupMembers(populateGroupMembers) {
      group.name = HeapStrDup(HeapBase::ProcessHeap(), name);
      error = group.name.IsValid() ? ERROR_SUCCESS : GetLastError();
    }

    ~getgrnam_worker() {}

  // passes the execution to getgrnam_impl
  void Execute() {
    if (error == ERROR_SUCCESS) {
      error = getgrnam_impl(group, populateGroupMembers);
    }
  }

  // called after an asynchronously called method (method_impl) has
  // finished to convert the results to JavaScript objects and pass
  // them to JavaScript callback
  void HandleOKCallback() {
    HandleScope scope;
    if (error == ERROR_NONE_MAPPED) {
      // pass the results to the external callback
      Local<Value> argv[] = {
        // in case of success, make the first argument (error) null
        Null(),
        // in case of success, populate the second and other arguments
        Undefined()
      };
      callback->Call(2, argv);
    } else if (error != ERROR_SUCCESS) {
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
        convert_group(group)
      };
      callback->Call(2, argv);
    }
  }

  private:
    bool populateGroupMembers;
    DWORD error;
    group_t group;
};

// the native entry point for the exposed getgrnam function
NAN_METHOD(getgrnam) {
  int argc = info.Length();
  if (argc < 1)
    return ThrowTypeError("name required");
  if (argc > 2)
    return ThrowTypeError("too many arguments");
  if (!info[0]->IsString())
    return ThrowTypeError("name must be a string");
  if (argc > 1 && !info[1]->IsFunction())
    return ThrowTypeError("callback must be a function");

  String::Utf8Value name(info[0]->ToString());
  bool populateGroupMembers = shall_populate_group_members(info);

  // if no callback was provided, assume the synchronous scenario,
  // call the method_sync immediately and return its results
  if (!info[1]->IsFunction()) {
    HandleScope scope;
    group_t group;
    group.name = HeapStrDup(HeapBase::ProcessHeap(), *name);
    if (!group.name.IsValid())
      return ThrowLastWinapiError();
    DWORD error = getgrnam_impl(group, populateGroupMembers);
    if (error == ERROR_NONE_MAPPED)
      return info.GetReturnValue().Set(Undefined());
    if (error != ERROR_SUCCESS)
      return ThrowWinapiError(error);
    return info.GetReturnValue().Set(convert_group(group));
  }

  // prepare parameters for the method_impl to be called later;
  // queue the worker to be called when posibble and send its
  // result to the external callback
  Callback * callback = new Callback(info[1].As<Function>());
  AsyncQueueWorker(new getgrnam_worker(callback, *name,
    populateGroupMembers));
}

// -------------------------------------------------
// getpwnam - gets user information for a user name:
// { name, passwd, uid, gid, gecos, shell, dir }  getpwnam( name, [callback] )

// completes the user information using the name member of it
static DWORD getpwnam_impl(user_t & user) {
  HeapMem<PSID> usid;
  DWORD error = resolve_name(user.name, &usid);
  if (error != ERROR_SUCCESS) {
    return error;
  }
  return resolve_user(user, usid);
}

// passes input/output parameters between the native method entry point
// and the worker method doing the work, which is called asynchronously
class getpwnam_worker : public AsyncWorker {
  public:
    getpwnam_worker(Callback * callback, LPSTR name) : AsyncWorker(callback) {
      user.name = HeapStrDup(HeapBase::ProcessHeap(), name);
      error = user.name.IsValid() ? ERROR_SUCCESS : GetLastError();
    }

    ~getpwnam_worker() {}

  // passes the execution to getpwnam_impl
  void Execute() {
    if (error == ERROR_SUCCESS) {
      error = getpwnam_impl(user);
    }
  }

  // called after an asynchronously called method (method_impl) has
  // finished to convert the results to JavaScript objects and pass
  // them to JavaScript callback
  void HandleOKCallback() {
    HandleScope scope;
    if (error == ERROR_NONE_MAPPED) {
      // pass the results to the external callback
      Local<Value> argv[] = {
        // in case of success, make the first argument (error) null
        Null(),
        // in case of success, populate the second and other arguments
        Undefined()
      };
      callback->Call(2, argv);
    } else if (error != ERROR_SUCCESS) {
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
        convert_user(user)
      };
      callback->Call(2, argv);
    }
  }

  private:
    DWORD error;
    user_t user;
};

// the native entry point for the exposed getpwnam function
NAN_METHOD(getpwnam) {
  int argc = info.Length();
  if (argc < 1)
    return ThrowTypeError("name required");
  if (argc > 2)
    return ThrowTypeError("too many arguments");
  if (!info[0]->IsString())
    return ThrowTypeError("name must be a string");
  if (argc > 1 && !info[1]->IsFunction())
    return ThrowTypeError("callback must be a function");

  String::Utf8Value name(info[0]->ToString());

  // if no callback was provided, assume the synchronous scenario,
  // call the method_sync immediately and return its results
  if (!info[1]->IsFunction()) {
    HandleScope scope;
    user_t user;
    user.name = HeapStrDup(HeapBase::ProcessHeap(), *name);
    if (!user.name.IsValid())
      return ThrowLastWinapiError();
    DWORD error = getpwnam_impl(user);
    if (error == ERROR_NONE_MAPPED)
      return info.GetReturnValue().Set(Undefined());
    if (error != ERROR_SUCCESS)
      return ThrowWinapiError(error);
    return info.GetReturnValue().Set(convert_user(user));
  }

  // prepare parameters for the method_impl to be called later;
  // queue the worker to be called when posibble and send its
  // result to the external callback
  Callback * callback = new Callback(info[1].As<Function>());
  AsyncQueueWorker(new getpwnam_worker(callback, *name));
}

// ------------------------------------------------
// getpwuid - gets user information for a user SID:
// { name, passwd, uid, gid, gecos, shell, dir }  getpwuid( uid, [callback] )

// completes the user information using the uid (string) member of it
static DWORD getpwuid_impl(user_t & user) {
  LocalMem<PSID> usid;
  if (ConvertStringSidToSid(user.uid, &usid) == FALSE) {
    return GetLastError();
  }

  user.uid.Dispose();

  return resolve_user(user, usid);
}

// passes input/output parameters between the native method entry point
// and the worker method doing the work, which is called asynchronously
class getpwuid_worker : public AsyncWorker {
  public:
    getpwuid_worker(Callback * callback, LPSTR uid) : AsyncWorker(callback) {
      user.uid = LocalStrDup(uid);
      error = user.uid.IsValid() ? ERROR_SUCCESS : GetLastError();
    }

    ~getpwuid_worker() {}

  // passes the execution to getpwuid_impl
  void Execute() {
    if (error == ERROR_SUCCESS) {
      error = getpwuid_impl(user);
    }
  }

  // called after an asynchronously called method (method_impl) has
  // finished to convert the results to JavaScript objects and pass
  // them to JavaScript callback
  void HandleOKCallback() {
    HandleScope scope;
    if (error == ERROR_NONE_MAPPED) {
      // pass the results to the external callback
      Local<Value> argv[] = {
        // in case of success, make the first argument (error) null
        Null(),
        // in case of success, populate the second and other arguments
        Undefined()
      };
      callback->Call(2, argv);
    } else if (error != ERROR_SUCCESS) {
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
        convert_user(user)
      };
      callback->Call(2, argv);
    }
  }

  private:
    DWORD error;
    user_t user;
};

// the native entry point for the exposed getpwuid function
NAN_METHOD(getpwuid) {
  int argc = info.Length();
  if (argc < 1)
    return ThrowTypeError("uid required");
  if (argc > 2)
    return ThrowTypeError("too many arguments");
  if (!info[0]->IsString())
    return ThrowTypeError("uid must be a string");
  if (argc > 1 && !info[1]->IsFunction())
    return ThrowTypeError("callback must be a function");

  String::Utf8Value uid(info[0]->ToString());

  // if no callback was provided, assume the synchronous scenario,
  // call the method_sync immediately and return its results
  if (!info[1]->IsFunction()) {
    HandleScope scope;
    user_t user;
    user.uid = LocalStrDup(*uid);
    if (!user.uid.IsValid())
      return ThrowLastWinapiError();
    DWORD error = getpwuid_impl(user);
    if (error == ERROR_NONE_MAPPED)
      return info.GetReturnValue().Set(Undefined());
    if (error != ERROR_SUCCESS)
      return ThrowWinapiError(error);
    return info.GetReturnValue().Set(convert_user(user));
  }

  // prepare parameters for the method_impl to be called later;
  // queue the worker to be called when posibble and send its
  // result to the external callback
  Callback * callback = new Callback(info[1].As<Function>());
  AsyncQueueWorker(new getpwuid_worker(callback, *uid));
}

// exposes methods implemented by this sub-package and initializes the
// string symbols for the converted resulting object literals; to be
// called from the add-on module-initializing function
NAN_MODULE_INIT(init) {
  NAN_EXPORT(target, getgrgid);
  NAN_EXPORT(target, getgrnam);
  NAN_EXPORT(target, getpwnam);
  NAN_EXPORT(target, getpwuid);
}

} // namespace posix_win
