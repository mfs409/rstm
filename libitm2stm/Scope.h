/** -*- C++ -*-
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#ifndef STM_ITM2STM_SCOPE_H
#define STM_ITM2STM_SCOPE_H

#include <utility>            // std::pair
#include "libitm.h"           // _ITM_ stuff
#include "Checkpoint.h"       // class Checkpoint
#include "stm/MiniVector.hpp"

namespace itm2stm {
/// A Scope maintains the data associated with a nested transaction. This
/// includes the transaction's checkpoint, the flags that it began with, a flag
/// that tells us if it has been aborted (an ABI-required behavior), an address
/// range used to register a thrown exception, lists of user-registered onUndo
/// and onCommit handlers, and a list of logged values.
///
/// The runtime can commit a scope, rollback a scope, and restore the scope's
/// checkpoint (which performs a longjmp).
class Scope : public Checkpoint /* asm needs this as first superclass */
{
  public:
    Scope(_ITM_transaction&);

    /// Read access to the Scope's id. The id is set during the Scope::enter
    /// call from a parameter.
    _ITM_transactionId_t getId() const {
        return id_;
    }

    /// Used by the Transaction during a restart, to simplify re-calling
    /// Scope::enter during Transaction::restart.
    uint32_t getFlags() const {
        return flags_;
    }

    /// Used by the Transaction's rollback functionality.
    bool isExceptionBlock() const {
        return flags_ & pr_exceptionBlock;
    }

    /// Read/write accessors for the aborted flag. The write accessor is only
    /// used by the Transaction during rollback when it finds that it must mark
    /// the "outermost" scope as aborted, before the outermost scope gets
    /// completely rolled back. This is an ITM-ABI required behavior.
    void setAborted(bool val) {
        aborted_ = val;
    }

    bool getAborted() const {
        return aborted_;
    }

    /// Used from the libstm conflict abort handler
    /// (itm2stm-5.7.cpp:stm::TxThread::tmabort).
    _ITM_transaction& getOwner() const {
        return owner_;
    }

    /// Called every time that a transaction begins (includes outer transactions
    /// and nested transactions, on their first entry and on every
    /// restart). After this call the scope must be entirely clean and ready to
    /// go. We inline this because it is important for performance in a number
    /// of places.
    void enter(_ITM_transactionId_t id, uint32_t flags) {
        id_ = id;
        flags_ = flags;
        aborted_ = false;
        thrown_.reset();
    }

    /// Called when a transaction is either aborted or restarted. From the
    /// scope's perspective there isn't any difference between these two
    /// operations. We don't really care about rollback performance so this is
    /// outlined. The return value is a pair indicating a range of addresses
    /// that should not be rolled back by the library---this corresponds with a
    /// registered thrown object, but there isn't any reason to expose the
    /// ThrownObject type externally. If there was no thrown object then the
    /// returned value will be the pair (NULL, 0).
    ///
    /// The stack maintains an undo log in support of the _ITM_L* calls.
    std::pair<void**, size_t>& rollback();

    /// Called to commit a scope. Inlined because we care about commit
    /// performance.
    void commit() {
        for (CommitList::iterator i = do_on_commit_.begin(),
                                  e = do_on_commit_.end(); i != e; ++i)
            i->eval();
        do_on_rollback_.reset();
        undo_on_rollback_.reset();
        // don't reset thrown, it's reset by Scope::enter.
    }

    /// Self explanatory, precondition thrown_.address == NULL.
    void setThrownObject(void** addr, size_t length);

    // Resets the thrown object.
    void clearThrownObject();

    /// This is called from the logging functions (_ITM_L*), and forwards to
    /// the SHIM_LOG_HELPER template to perform the logging. It's also used to
    /// log stack accesses from nested transactions (from _ITM_W*).
    template <typename T>
    void log(const T* address) {
        SHIM_LOG_HELPER<T>::log(this, address);
    }

    /// This version of log is used directly by the _ITM_LB routine, and also
    /// supports the SHIM_LOG_HELPER template.
    void log(void** addr, void* value, size_t bytes) {
        undo_on_rollback_.insert(LoggedWord(addr, value, bytes));
    }

    /// Registration handler is trivial so we inline it.
    void registerOnCommit(_ITM_userCommitFunction f, void* arg) {
        do_on_commit_.insert(make_callback(f, arg));
    }

    /// Registration handler is trivial so we inline it.
    void registerOnAbort(_ITM_userUndoFunction f, void* arg) {
        do_on_rollback_.insert(make_callback(f, arg));
    }

  private:
    /// The ITM interface is designed to register thrown objects to support
    /// abort-on-throw semantics. This pair represent such a thrown-object
    /// address range.
    struct ThrownObject : public std::pair<void**, size_t> {
        void** begin() const;
        void** end() const;
        void reset() {
            first = NULL;
            second = 0;
        }
    };

    /// ITM allows users to register onCommit and onAbort handlers to execute
    /// user code during those events. The ITM interface defines the callbacks
    /// with independent type name (userCommitAction and userAbortAction), even
    /// though they are structurally equivalent (void (*)(void*)). We don't want
    /// to rely on the structural equivalence, so we use this templated struct
    /// to store and evaluate both types of callbacks.
    template <typename F>
    struct Callback {
        F function_;
        void*  arg_;

        Callback(F f, void* arg) : function_(f), arg_(arg) {
        }

        void eval() const {
            function_(arg_);
        }
    };

    /// Standard metaprogramming type-dispatcher picks up the necessary type
    /// from the parameter (see std::make_pair);
    template <typename F>
    static Callback<F> make_callback(F f, void* arg) {
        return Callback<F>(f, arg);
    }

    /// ITM will sometimes want to log a thread-local value, but instead of
    /// using stack-space and well-known control flow, it will ask the library
    /// to perform the logging on its behalf. We could simply use existing
    /// transactional metadata structures to log them, but these logged values
    /// do not need conflict detection, and we don't expect them to be
    /// common. For this reason, we do simple logging in the shim, and undo the
    /// logged values during rollback.
    ///
    /// We log in word-sized maximum chunks. *These are not assumed to have any
    /// specific alignment.*
    struct LoggedWord {
      private:
        void** address_;
        void*    value_;
        size_t   bytes_;

        /// The clip routine is used to protect against undoing to thrown
        /// objects. We aren't capable of undoing to a discontinuous range
        /// (i.e., if the logged value is larger than the range, and the range
        /// is completely contained withing the logged address range, and there
        /// is some "left-over" space on each side).
        ///
        /// The range is [lower, upper)
        void clip(void** lower, void** upper);

      public:
        LoggedWord(void** addr, void* val, size_t bytes)
            : address_(addr), value_(val), bytes_(bytes) {
        }

        void** begin() const;
        void** end() const;
        void undo(ThrownObject&);
    };

    typedef stm::MiniVector<Callback<_ITM_userUndoFunction> > RollbackList;
    typedef stm::MiniVector<Callback<_ITM_userCommitFunction> > CommitList;
    typedef stm::MiniVector<LoggedWord> UndoList;

    bool                aborted_;
    uint32_t              flags_;
    _ITM_transactionId_t     id_;
    ThrownObject         thrown_;
    RollbackList do_on_rollback_;
    UndoList   undo_on_rollback_;
    CommitList     do_on_commit_;
    _ITM_transaction&     owner_; // this is needed to handle conflict
                                  // aborts---see
                                  // itm2stm-5.8.cpp:stm::TxThread::tmabort

    /// This template provides a nice interface to the shim's logging
    /// capabilities. It chunks the logged type into words, and logs them all
    /// individually. If the logged type is just 1 word we expect that the loop
    /// will be eliminated.
    ///
    /// We specialize for subword types where W == 0 later.
    template <typename T, size_t W = sizeof(T) / sizeof(void*)>
    struct SHIM_LOG_HELPER {
        static void log(Scope* const scope, const T* addr) {
            void** address = reinterpret_cast<void**>(const_cast<T*>(addr));
            for (size_t i = 0; i < W; ++i)
                scope->log(address, *address, sizeof(void*));
        }
    };

    /// This is the specialized logger for subword types.
    template <typename T>
    struct SHIM_LOG_HELPER<T, 0u> {
        static void log(Scope* const scope, const T* addr) {
            void** address = reinterpret_cast<void**>(const_cast<T*>(addr));
            union {
                T val;
                void* word;
            } cast = { *addr };
            scope->log(address, cast.word, sizeof(T));
        }
    };
};
} // namespace itm2stm

#endif // STM_ITM2STM_SCOPE_H
