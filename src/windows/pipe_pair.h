/**
 * @file	pipe_pair.cc
 * @author	Joseph Lee <development@jc-lab.net>
 * @date	2020/08/07
 * @copyright Copyright (C) 2020 jc-lab.\n
 *            This software may be modified and distributed under the terms
 *            of the Apache License 2.0.  See the LICENSE file for details.
 */

#ifndef JCU_PROCESS_SRC_WINDOWS_PIPE_PAIR_H_
#define JCU_PROCESS_SRC_WINDOWS_PIPE_PAIR_H_

#include <tchar.h>
#include <windows.h>

namespace jcu {
namespace process {
namespace windows {

class PipePair {
 private:
  DWORD err_;

  HANDLE wr_;
  HANDLE rd_;

 public:
  PipePair();
  ~PipePair();

  DWORD getError() const {
    return err_;
  }

  void close();
  HANDLE getWriteHandle();
  HANDLE getReadHandle();
  HANDLE detachWriteHandle();
  HANDLE detachReadHandle();
  HANDLE detachInheritableWriteHandle();
  HANDLE detachInheritableReadHandle();

};

} // namespace windows
} // namespace process
} // namespace jcu

#endif //JCU_PROCESS_SRC_WINDOWS_PIPE_PAIR_H_
