/**
 * @file	process.h
 * @author	Joseph Lee <development@jc-lab.net>
 * @date	2020/08/06
 * @copyright Copyright (C) 2020 jc-lab.\n
 *            This software may be modified and distributed under the terms
 *            of the Apache License 2.0.  See the LICENSE file for details.
 */

#ifndef JCU_PROCESS_SRC_PROCESS_H_
#define JCU_PROCESS_SRC_PROCESS_H_

#include <functional>

namespace jcu {
namespace process {

enum EventType {
  kProcessExited = 0x0001,
  kReadStdout = 0x0010,
  kReadStderr = 0x0020,
};

enum EventLoopResult {
  kEventLoopError = -1,
  kEventLoopDone = 0,
  kEventLoopIdle = 1,
  kEventLoopHandled = 2,
};

typedef std::function<void(EventType event_type, const char* buffer, size_t length)> EventLoopHandler_t;

class Process {
 public:
  virtual int execute() = 0;

  /**
   *
   * @return
   * - -1 : Error
   * - 0 : Done
   * - 1 : Running... (Maybe need throttling)
   * - 2 : Event Handled...
   */
  virtual EventLoopResult eventloop(const EventLoopHandler_t& handler, int *perr = nullptr) = 0;

  virtual bool isAlive() const = 0;
  virtual bool checkAlive() = 0;

  virtual int getExitCode() const = 0;

  virtual int terminateProcess() = 0;

 public:
  static Process* prepare(const char *command);
#ifdef _UNICODE
  static Process* prepare_w(const wchar_t *command);
#endif
};

} // namespace process
} // namespace jcu

#endif //JCU_PROCESS_SRC_PROCESS_H_
