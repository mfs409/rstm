/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */
#ifndef RSTM_USER_CALLBACK_LOG_H
#define RSTM_USER_CALLBACK_LOG_H

/**
 *  A log structure that supports ITM's user callback on commit/undo
 *  infrastructure.
 */

#include <utility>
#include <algorithm>
#include "MiniVector.hpp"
#include "libitm.h"

namespace stm {
  class UserCallbackLog {
    public:
      UserCallbackLog(int init = 4) : onCommit_(init), onRollback_(init) {
      }

      void doOnCommit(_ITM_userCommitFunction f, void* arg) {
          PushBack(onCommit_, f, arg);
      }

      void doOnRollback(_ITM_userUndoFunction f, void* arg) {
          PushBack(onRollback_, f, arg);
      }

      void onCommit() {
          if (onCommit_.size())
              DoForEach(onCommit_.begin(), onRollback_.end());
          reset();
      }

      void onRollback() {
          if (onRollback_.size())
              DoForEach(onRollback_.rbegin(), onRollback_.rend());
          reset();
      }

    private:
      typedef void (*UserCallbackFn)(void*);
      typedef std::pair<UserCallbackFn, void*> UserCallback;
      typedef MiniVector<UserCallback> CallbackList;

      // Use 2 logs instead of one mixed log so that we can quickly test for
      // empty lists.
      CallbackList onCommit_;
      CallbackList onRollback_;

      void reset() {
          onRollback_.reset();
          onCommit_.reset();
      }

      template <typename I>
      static void DoForEach(I begin, I end) {
          std::for_each(begin, end, DoCallback);
      }

      static void PushBack(CallbackList& list, UserCallbackFn f, void* a) {
          list.push_back(std::make_pair(f, a));
      }

      static void DoCallback(const UserCallback& f) {
          f.first(f.second);
      }
  };
}

#endif // RSTM_USER_FUNCTION_LOG_H
