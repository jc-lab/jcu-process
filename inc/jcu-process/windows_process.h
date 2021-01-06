/**
 * @file	windows_process.h
 * @author	Joseph Lee <development@jc-lab.net>
 * @date	2021/01/06
 * @copyright Copyright (C) 2020 jc-lab.\n
 *            This software may be modified and distributed under the terms
 *            of the Apache License 2.0.  See the LICENSE file for details.
 */

#ifndef JCU_PROCESS_WINDOWS_PROCESS_H_
#define JCU_PROCESS_WINDOWS_PROCESS_H_
#ifdef _WIN32

#include <windows.h>

#include "process.h"

namespace jcu {
namespace process {

class WindowsProcess : public Process {
 public:
  typedef std::function<DWORD (wchar_t* command_line_buf, STARTUPINFOW* si, PROCESS_INFORMATION* pi)> CustomCreateProcess;
  virtual void setCustomCreateProcess(const CustomCreateProcess& supplier) = 0;
};

} // namespace process
} // namespace jcu

#endif //_WIN32
#endif //JCU_PROCESS_WINDOWS_PROCESS_H_
