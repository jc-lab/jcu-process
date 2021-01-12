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
#include <atomic>

#include <string.h>
#include <tchar.h>
#include <windows.h>

#include <jcu-process/process.h>
#include <jcu-process/windows_process.h>
#include "pipe_pair.h"

namespace jcu {
namespace process {

namespace windows {

static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>,wchar_t> s_utf8_to_wstr_convert;

class WindowsProcessImpl : public WindowsProcess {
 private:
  std::basic_string<wchar_t> command_line_;
  PROCESS_INFORMATION pi_;
  PipePair pipe_stdin_;
  PipePair pipe_stdout_;
  PipePair pipe_stderr_;

  bool process_alive_;
  int exit_code_;

  std::atomic_int interest_index_;

  CustomCreateProcess custom_create_process_;

  WindowsProcessImpl(const std::basic_string<wchar_t> &command_line)
  : command_line_(command_line), pi_({0}), process_alive_(false), exit_code_(-1), custom_create_process_(), interest_index_(0)
  { }

 public:
  virtual ~WindowsProcessImpl() {

  }

  void setCustomCreateProcess(const CustomCreateProcess& supplier) override {
    custom_create_process_ = supplier;
  }

  int execute() override {
    int err_code;
    DWORD cp_err;

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

    if (custom_create_process_) {
      cp_err = custom_create_process_(
          command_line_buf.data(),
          &si,
          &pi_
          );
    } else {
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
        cp_err = ::GetLastError();
      }
    }
    if (cp_err) {
      if (si.hStdInput) {
        ::CloseHandle(si.hStdInput);
      }
      if (si.hStdOutput) {
        ::CloseHandle(si.hStdOutput);
      }
      if (si.hStdError) {
        ::CloseHandle(si.hStdError);
      }
      return (int) cp_err;
    }

    process_alive_ = true;
    return 0;
  }

  EventLoopResult eventloop(const EventLoopHandler_t &handler, int *perr) override {
    std::pair<int, HANDLE> handles[3] = {
            std::pair<int, HANDLE>(0, pipe_stdout_.getReadHandle()),
            std::pair<int, HANDLE>(1, pipe_stderr_.getReadHandle()),
            std::pair<int, HANDLE>(2, pi_.hProcess)
    };
    int handle_types[3] = {1, 2, 3};
    int killed_process_chance = 0;
    do {
      int interest_index = interest_index_.fetch_add(1);
      int ordered_indexes[3] = {
        handles[(interest_index + 0) % 3].first,
        handles[(interest_index + 1) % 3].first,
        handles[(interest_index + 2) % 3].first
      };
      HANDLE ordered_handles[3] = {
        handles[(interest_index + 0) % 3].second,
        handles[(interest_index + 1) % 3].second,
        handles[(interest_index + 2) % 3].second
      };

      char buffer[128] = {0};
      int processed_size = 0;
      DWORD dwWait = ::WaitForMultipleObjects(3, ordered_handles, FALSE, 100);
      if (dwWait == WAIT_TIMEOUT) {
        return kEventLoopIdle;
      } else if ((dwWait < 0) || (dwWait > (WAIT_OBJECT_0 + 2))) {
        if (perr) *perr = ::GetLastError();
        return kEventLoopError;
      }

      int handle_index = ordered_indexes[dwWait - WAIT_OBJECT_0];
      int is_stdout = handle_index == 0;
      int is_stderr = handle_index == 1;
      int is_proc = handle_index == 2;

      HANDLE target_handle = handles[handle_index].second;
      DWORD pipe_read_bytes = 0;
      DWORD pipe_total_bytes_avail = 0;
      DWORD pipe_bytes_left_this_message = 0;
      if (is_stdout || is_stderr) {
        if (::PeekNamedPipe(target_handle, buffer, sizeof(buffer), &pipe_read_bytes, &pipe_total_bytes_avail, &pipe_bytes_left_this_message)) {
          if (pipe_read_bytes > 0) {
            if (::ReadFile(target_handle, buffer, sizeof(buffer), &pipe_read_bytes, nullptr)) {
              processed_size = pipe_read_bytes;
              if (is_stdout) {
                handler(kReadStdout, buffer, processed_size);
              } else if (is_stderr) {
                handler(kReadStderr, buffer, processed_size);
              }
              return kEventLoopHandled;
            }
          } else {
            handle_index = -1;
          }
        }
      }

      if (handle_index == -1 || is_proc) {
        if (handle_index != -1) {
          killed_process_chance = 0;
        }
        dwWait = ::WaitForSingleObject(pi_.hProcess, 100);
        if (dwWait == WAIT_OBJECT_0) {
          is_proc = true;
        }
      }
      if (is_proc) {
        killed_process_chance++;
        if (killed_process_chance >= 2) {
          DWORD exit_code = 0;
          process_alive_ = false;
          if (::GetExitCodeProcess(pi_.hProcess, &exit_code)) {
            exit_code_ = (int) exit_code;
          }
          handler(kProcessExited, buffer, processed_size);
          return kEventLoopDone;
        }
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

  int getExitCode() const override {
    return exit_code_;
  }

  int terminateProcess() override {
    if (!::TerminateProcess(pi_.hProcess, 1)) {
      return (int)::GetLastError();
    }
    return 0;
  }

  int writeToStdin(const char* data, int length) override {
    DWORD number_of_bytes_written = 0;
    if (!::WriteFile(pipe_stdin_.getWriteHandle(), data, length, &number_of_bytes_written, nullptr)) {
      return (int) ::GetLastError();
    }
    return 0;
  }

 public:
  static Process *prepare(const std::basic_string<wchar_t>& command_line) {
    return new WindowsProcessImpl(command_line);
  }
};

}

Process* Process::prepare(const char *command) {
  return windows::WindowsProcessImpl::prepare(
      windows::s_utf8_to_wstr_convert.from_bytes(command)
      );
}

#ifdef _UNICODE
Process* Process::prepare_w(const wchar_t *command) {
  return windows::WindowsProcessImpl::prepare(command);
}
#endif


} // namespace process
} // namespace jcu
