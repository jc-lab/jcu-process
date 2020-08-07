/**
 * @file	pipe_pair.h
 * @author	Joseph Lee <development@jc-lab.net>
 * @date	2020/08/07
 * @copyright Copyright (C) 2020 jc-lab.\n
 *            This software may be modified and distributed under the terms
 *            of the Apache License 2.0.  See the LICENSE file for details.
 */

#include <atomic>
#include <vector>

#include <stdio.h>
#include <windows.h>
#include <accctrl.h>
#include <aclapi.h>

#include "pipe_pair.h"

namespace jcu {
namespace process {
namespace windows {

int nextKey() {
  static std::atomic_int v(0);
  return v.fetch_add(1);
}

class CurrentUserToken {
 private:
  std::vector<BYTE> token_user_buf_;

 public:
  CurrentUserToken() {}

  DWORD init() {
    DWORD result = 0;
    HANDLE token_handle = NULL;

    do {
      DWORD buf_size = 0;

      if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token_handle)) {
        result = ::GetLastError();
        break;
      }

      if (!GetTokenInformation(token_handle, TokenUser, NULL, 0, &buf_size) &&
          (GetLastError() != ERROR_INSUFFICIENT_BUFFER)) {
        result = ::GetLastError();
        break;
      }

      token_user_buf_.resize(buf_size);
      PTOKEN_USER token_user = reinterpret_cast<PTOKEN_USER>( &token_user_buf_[0] );

      //
      // Retrieve the token information in a TOKEN_USER structure
      //
      if (!GetTokenInformation(
          token_handle,
          TokenUser,
          token_user,
          buf_size,
          &buf_size)) {
        result = ::GetLastError();
        break;
      }
    } while (0);

    if (token_handle && (token_handle != INVALID_HANDLE_VALUE)) {
      ::CloseHandle(token_handle);
    }

    return result;
  }

  PTOKEN_USER getTokenUser() const {
    return (PTOKEN_USER) token_user_buf_.data();
  }
};

class SecurityDescriptorHelper {
 public:
  PACL acl_;
  PSECURITY_DESCRIPTOR sd_;

  SecurityDescriptorHelper()
      : acl_(nullptr), sd_(nullptr) {
  }

  ~SecurityDescriptorHelper() {
    cleanup(&sd_, &acl_);
  }

  PSECURITY_DESCRIPTOR getSecurityDescriptor() const {
    return sd_;
  }

  DWORD fromExplicitAccesses(EXPLICIT_ACCESS *ea, int count) {
    DWORD result;
    PACL new_acl = nullptr;
    PSECURITY_DESCRIPTOR new_sd = nullptr;

    do {
      // Create a new ACL that contains the new ACEs.
      result = ::SetEntriesInAcl(count, ea, NULL, &new_acl);
      if (result != ERROR_SUCCESS) {
        break;
      }

      // Initialize a security descriptor.
      new_sd = (PSECURITY_DESCRIPTOR) LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
      if (!new_sd) {
        result = ::GetLastError();
        break;
      }

      if (!InitializeSecurityDescriptor(new_sd, SECURITY_DESCRIPTOR_REVISION)) {
        result = ::GetLastError();
        break;
      }

      // Add the ACL to the security descriptor.
      if (!SetSecurityDescriptorDacl(
          new_sd,
          TRUE,// bDaclPresent flag
          new_acl,
          FALSE))// not a default DACL
      {
        result = ::GetLastError();
        break;
      }
    } while (0);

    if (result == ERROR_SUCCESS) {
      sd_ = new_sd;
      acl_ = new_acl;
    } else {
      cleanup(&new_sd, &new_acl);
    }

    return result;
  }

 private:
  void cleanup(PSECURITY_DESCRIPTOR *ppsd, PACL *ppacl) {
    if (ppsd && *ppsd) {
      ::LocalFree(*ppsd);
      *ppsd = nullptr;
    }
    if (ppacl && *ppacl) {
      ::LocalFree(*ppacl);
      *ppacl = nullptr;
    }
  }
};

PipePair::PipePair()
    : err_(0), rd_(nullptr), wr_(nullptr) {
  CurrentUserToken current_user_token;
  SecurityDescriptorHelper sd_helper;
  EXPLICIT_ACCESS ea[1] = {0};

  TCHAR pipename[MAX_PATH];
  // remote process anonymous
  _stprintf_s(pipename, _T("\\\\.\\pipe\\rpa.%04x%04x%08x%08x"),
              ::GetCurrentProcessId() & 0xffff,
              ::GetCurrentThreadId() & 0xffff,
              ::GetTickCount(),
              nextKey()
  );

  err_ = current_user_token.init();
  if (err_) {
    return;
  }

  PTOKEN_USER token_user = current_user_token.getTokenUser();
  ea[0].grfAccessPermissions = GENERIC_ALL;
  ea[0].grfAccessMode = SET_ACCESS;
  ea[0].grfInheritance = NO_INHERITANCE;
  ea[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
  ea[0].Trustee.TrusteeType = TRUSTEE_IS_USER;
  ea[0].Trustee.ptstrName = (LPTSTR) token_user->User.Sid;
  err_ = sd_helper.fromExplicitAccesses(ea, 1);
  if (err_) {
    return;
  }

  SECURITY_ATTRIBUTES sa = {0};
  sa.nLength = sizeof(sa);
  sa.lpSecurityDescriptor = sd_helper.getSecurityDescriptor();
  sa.bInheritHandle = FALSE;

  rd_ = ::CreateNamedPipe(
      pipename,
      PIPE_ACCESS_INBOUND,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE,
      1,
      128,
      128,
      3000,
      &sa
  );
  if (!rd_ || (rd_ == INVALID_HANDLE_VALUE)) {
    err_ = ::GetLastError();
    return;
  }

  wr_ = ::CreateFile(
      pipename,
      GENERIC_WRITE,
      0,
      nullptr,
      OPEN_EXISTING,
      FILE_FLAG_NO_BUFFERING,
      nullptr
  );
  if (!wr_ || (wr_ == INVALID_HANDLE_VALUE)) {
    err_ = ::GetLastError();
    return;
  }
}
PipePair::~PipePair() {
  close();
}
void PipePair::close() {
  if (wr_ && wr_ != INVALID_HANDLE_VALUE) {
    ::CloseHandle(wr_);
    wr_ = nullptr;
  }
  if (rd_ && rd_ != INVALID_HANDLE_VALUE) {
    ::CloseHandle(rd_);
    rd_ = nullptr;
  }
}
HANDLE PipePair::getWriteHandle() {
  return wr_;
}
HANDLE PipePair::getReadHandle() {
  return rd_;
}
HANDLE PipePair::detachWriteHandle() {
  HANDLE h = wr_;
  wr_ = nullptr;
  return h;
}
HANDLE PipePair::detachReadHandle() {
  HANDLE h = rd_;
  rd_ = nullptr;
  return h;
}
HANDLE PipePair::detachInheritableWriteHandle() {
  HANDLE handle = nullptr;
  if (!DuplicateHandle(
      GetCurrentProcess(), wr_,
      GetCurrentProcess(), &handle, 0,
      TRUE, DUPLICATE_SAME_ACCESS)) {
    err_ = ::GetLastError();
    return nullptr;
  }
  wr_ = nullptr;
  return handle;
}
HANDLE PipePair::detachInheritableReadHandle() {
  HANDLE handle = nullptr;
  if (!DuplicateHandle(
      GetCurrentProcess(), rd_,
      GetCurrentProcess(), &handle, 0,
      TRUE, DUPLICATE_SAME_ACCESS)) {
    err_ = ::GetLastError();
    return nullptr;
  }
  rd_ = nullptr;
  return handle;
}

} // namespace windows
} // namespace process
} // namespace jcu
