/**
 * @file	process.cc
 * @author	Joseph Lee <development@jc-lab.net>
 * @date	2020/08/06
 * @copyright Copyright (C) 2020 jc-lab.\n
 *            This software may be modified and distributed under the terms
 *            of the Apache License 2.0.  See the LICENSE file for details.
 */

#include <string>
#include <vector>
#include <codecvt>

#include <string.h>
#include <tchar.h>
#include <windows.h>

#include <jcu-process/process.h>
#include "pipe_pair.h"

namespace jcu {
namespace process {

namespace windows {

static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>,wchar_t> s_utf8_to_wstr_convert;

class WindowsProcess : public Process {
 private:
  std::basic_string<wchar_t> command_line_;
  PROCESS_INFORMATION pi_;
  PipePair pipe_stdin_;
  PipePair pipe_stdout_;
  PipePair pipe_stderr_;

  bool process_alive_;

  WindowsProcess(const std::basic_string<wchar_t> &command_line)
  : command_line_(command_line), pi_({0}), process_alive_(false)
  { }

 public:
  virtual ~WindowsProcess() {

  }

  int execute() override {
    int err_code;

    std::vector<wchar_t> command_line_buf(command_line_.length() + 1);
    ::memcpy(command_line_buf.data(), command_line_.c_str(), command_line_.size() * sizeof(wchar_t));
    command_line_buf[command_line_.length()] = 0;

    STARTUPINFOW si = {0};
    ::memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;

    err_code = pipe_stdin_.getError();
    if (err_code) return err_code;
    err_code = pipe_stdout_.getError();
    if (err_code) return err_code;
    err_code = pipe_stderr_.getError();
    if (err_code) return err_code;

    si.hStdInput = pipe_stdin_.detachInheritableReadHandle();
    si.hStdOutput = pipe_stdout_.detachInheritableWriteHandle();
    si.hStdError = pipe_stderr_.detachInheritableWriteHandle();

    if(!::CreateProcessW(
        nullptr,
        command_line_buf.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NEW_CONSOLE,
        nullptr,
        nullptr,
        &si,
        &pi_
        )) {
      if (si.hStdInput) {
        ::CloseHandle(si.hStdInput);
      }
      if (si.hStdOutput) {
        ::CloseHandle(si.hStdOutput);
      }
      if (si.hStdError) {
        ::CloseHandle(si.hStdError);
      }
      return ::GetLastError();
    }

    process_alive_ = true;
    return 0;
  }

  EventLoopResult eventloop(const EventLoopHandler_t &handler, int *perr) override {
    HANDLE handles[3] = {
        pipe_stdout_.getReadHandle(), pipe_stderr_.getReadHandle(), pi_.hProcess
    };
    int killed_process_chance = 0;
    do {
      char buffer[128] = {0};
      int processed_size = 0;
      DWORD dwWait = ::WaitForMultipleObjects(3, handles, FALSE, 100);
      if (dwWait == WAIT_TIMEOUT) {
        return kEventLoopIdle;
      } else if ((dwWait < 0) || (dwWait > (WAIT_OBJECT_0 + 2))) {
        if (perr) *perr = ::GetLastError();
        return kEventLoopError;
      }

      int handle_index = dwWait - WAIT_OBJECT_0;
      HANDLE target_handle = handles[handle_index];
      DWORD pipe_read_bytes = 0;
      DWORD pipe_total_bytes_avail = 0;
      DWORD pipe_bytes_left_this_message = 0;
      if ((handle_index == 0) || (handle_index == 1)) {
        if (::PeekNamedPipe(target_handle, buffer, sizeof(buffer), &pipe_read_bytes, &pipe_total_bytes_avail, &pipe_bytes_left_this_message)) {
          if (pipe_read_bytes > 0) {
            if (::ReadFile(target_handle, buffer, sizeof(buffer), &pipe_read_bytes, nullptr)) {
              processed_size = pipe_read_bytes;
              switch (dwWait) {
                case (WAIT_OBJECT_0):
                  handler(kReadStdout, buffer, processed_size);
                  break;
                case (WAIT_OBJECT_0 + 1):
                  handler(kReadStderr, buffer, processed_size);
                  break;
              }
              return kEventLoopHandled;
            }
          } else {
            handle_index = -1;
          }
        }
      }

      if (handle_index < 2) {
        if (handle_index != -1) {
          killed_process_chance = 0;
        }
        dwWait = ::WaitForSingleObject(pi_.hProcess, 100);
        if (dwWait == WAIT_OBJECT_0) {
          dwWait = (WAIT_OBJECT_0 + 2);
        }
      }

      switch (dwWait) {
        case (WAIT_OBJECT_0 + 2):
          killed_process_chance++;
          if (killed_process_chance >= 2) {
            process_alive_ = false;
            handler(kProcessExited, buffer, processed_size);
            return kEventLoopDone;
          }
          break;
      }
    } while (killed_process_chance > 0);
    return kEventLoopIdle;
  }

  bool isAlive() const override {
    if (!process_alive_) {
      return false;
    }

    DWORD dwWait = ::WaitForSingleObject(pi_.hProcess, 0);
    if (dwWait == WAIT_OBJECT_0) {
      return false;
    }

    return true;
  }

  bool checkAlive() override {
    if (!process_alive_) {
      return false;
    }
    bool alive = isAlive();
    process_alive_ = alive;
    return alive;
  }

  int terminateProcess() override {
    if (!::TerminateProcess(pi_.hProcess, 1)) {
      return (int)::GetLastError();
    }
    return 0;
  }

 public:
  static Process *prepare(const std::basic_string<wchar_t>& command_line) {
    return new WindowsProcess(command_line);
  }
};

}

Process* Process::prepare(const char *command) {
  return windows::WindowsProcess::prepare(
      windows::s_utf8_to_wstr_convert.from_bytes(command)
      );
}

#ifdef _UNICODE
Process* Process::prepare_w(const wchar_t *command) {
  return windows::WindowsProcess::prepare(command);
}
#endif


} // namespace process
} // namespace jcu
