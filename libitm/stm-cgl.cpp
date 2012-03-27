///
/// Copyright (C) 2012
/// University of Rochester Department of Computer Science
///   and
/// Lehigh University Department of Computer Science and Engineering
///
/// License: Modified BSD
///          Please see the file LICENSE.RSTM for licensing information
///


///
/// This is an example native itm STM that demonstrats the normal control flow
/// around transactions. It is designed to correctly handle nested,
/// non-irrevocable transactions---though this hasn't been tested extensively.
///
/// It limits the amount of thread-local data to a single mcs queue-node. It
/// could use no per-thread data if we wanted to, or the depth/undo-log could be
/// thread-local (this would likely reduce cache misses).
///
/// It doesn't handle the entire ABI, but it does demonstrate the use of
/// libitm-dtfns.def to expand classes of ABI functions (currently READs and
/// WRITEs).
///
/// It uses some general-purpose voodoo to deal with logging non-word sized
/// data.

#include <cassert>                              // assert
#include <vector>                               // vector
#include "checkpoint.h"                         // post_checkpoint/_nested
#include "locks.h"                              // msc_lock_t
#include "word.h"                               // word_t
#include "libitm.h"                             // _ITM_*
using std::vector;
using rstm::word_t;
using rstm::mcs_qnode_t;

// All of this is anonymous.
namespace {
  ///
  /// We need an undo log for serial execution. We just use a union that stores
  /// the address, value, and number of bytes that are being logged. This entry
  /// can hold up to a word of data. Larger types need to be chunked into
  /// multiple log entries. (see UNDO_HELPER).
  ///
  /// TODO: for -m32 code, we don't want to store the uin64_t because it's too
  ///       big. we should deal with this somewhere.
  struct UndoEntry {
      union {
          struct {
              word_t* addr;
              word_t val;
          };
          struct {
              uint8_t* b_addr;
              uint8_t b_val[sizeof(word_t)/sizeof(uint8_t)];
          };
          struct {
              uint16_t* s_addr;
              uint16_t s_val[sizeof(word_t)/sizeof(uint16_t)];
          };
          struct {
              uint32_t* i_addr;
              uint32_t i_val[sizeof(word_t)/sizeof(uint32_t)];
          };
          struct {
              uint64_t* l_addr;
              uint64_t l_val[sizeof(word_t)/sizeof(uint64_t)];
          };
      };
      size_t bytes;

      UndoEntry() : addr(NULL), val(0), bytes(0) {
      }

      explicit UndoEntry(uint8_t* _addr) : b_addr(_addr), bytes(1) {
          b_val[0] = *b_addr;
      }

      explicit UndoEntry(uint16_t* _addr) : s_addr(_addr), bytes(2) {
          s_val[0] = *s_addr;
      }

      explicit UndoEntry(uint32_t* _addr) : i_addr(_addr), bytes(4) {
          i_val[0] = *i_addr;
      }

      explicit UndoEntry(uint64_t* _addr) : l_addr(_addr), bytes(8) {
          l_val[0] = *l_addr;
      }

      /// Undo switches on the number of bytes being stored, and calls the
      /// correct store function. We have to do this dynamically because we
      /// don't know what type we've logged, other than by this bytes value.
      void undo() const {
          if (__builtin_expect(bytes != sizeof(word_t), 0))
              goto subword;
          *addr = val;
          return;
        subword:
          switch (bytes) {
            case (1): *b_addr = b_val[0]; return;
            case (2): *s_addr = s_val[0]; return;
            case (4): *i_addr = i_val[0]; return;
            case (8): *l_addr = l_val[0]; return;
            case (0): assert(false && "Cannot redo uninitialized log.");
            default: assert(false && "Unexpected undo log size.");
          }
      }
  };

  ///
  /// For now we're using std::vectors for undo logging when we need to be able
  /// to abort.
  ///
  /// TODO: use something that supports a more efficient clear()/resize()
  /// operation.
  typedef vector<UndoEntry> UndoLog;
  static UndoLog undos;

  ///
  /// This template chunks undo operations into word-sized chunks.
  template <typename T, size_t B>
  struct LOG_HELPER;

  ///
  /// When the type, T, is a multiple of the word size, we perform multiple
  /// word_t sized log operations to remember it. The loop will be eliminated
  /// for single-word types.
  template <typename T>
  struct LOG_HELPER<T, 0u> {
      static void log(UndoLog& log, T* addr) {
          word_t* address = reinterpret_cast<word_t*>(addr);
          for (size_t i = 0; i < sizeof(T)/sizeof(word_t); ++i)
              log.push_back(UndoEntry(address + i));
      }
  };

  ///
  /// 1-byte type logging
  template <typename T>
  struct LOG_HELPER<T, 1u> {
      static void log(UndoLog& log, T* addr) {
          log.push_back(UndoEntry(reinterpret_cast<uint8_t*>(addr)));
      }
  };

  ///
  /// 2-byte type logging
  template <typename T>
  struct LOG_HELPER<T, 2u> {
      static void log(UndoLog& log, T* addr) {
          log.push_back(UndoEntry(reinterpret_cast<uint16_t*>(addr)));
      }
  };

  ///
  /// 4-byte type logging
  template <typename T>
  struct LOG_HELPER<T, 4u> {
      static void log(UndoLog& log, T* addr) {
          log.push_back(UndoEntry(reinterpret_cast<uint32_t*>(addr)));
      }
  };

  ///
  /// 8-byte type logging
  template <typename T>
  struct LOG_HELPER<T, 8u> {
      static void log(UndoLog& log, T* addr) {
          log.push_back(UndoEntry(reinterpret_cast<uint64_t*>(addr)));
      }
  };

  ///
  /// Just a utility that we call from the ITM barriers for logging.
  template <typename T>
  static void undo_log(T* address) {
      LOG_HELPER<T, sizeof(T) % sizeof(word_t)>::log(undos, address);
  }

  ///
  /// We just need an integer to keep track of our depth.
  static uint32_t depth = 0;

  ///
  /// Tracks the information we need for Serial execution where we might
  /// abort. Each Scope knows what depth it corresponds to, as well as contains
  /// the checkpoint that we need to longjmp to in order to get back to this
  /// scope. Finally, in order to abort, we need to be able to roll back our
  /// writes with the undo log. The scope keeps an index into the log that
  /// serves as the last entry to undo.
  struct Scope {
      uint32_t depth;
      int index;
      rstm::checkpoint_t checkpoint;

      Scope() : depth(), index(0), checkpoint() {
      }

      Scope(uint32_t d, int i)
          : depth(d), index(i), checkpoint() {
      }
  };

  ///
  /// We use a vector to keep track of our checkpoints, when necessary. We don't
  /// really need to clear this super-efficiently, because the commit protocol
  /// always pop_back's this structure.
  typedef vector<Scope> SerialScopeLog;
  static SerialScopeLog scopes;

  ///
  /// Finally, being CGL, we need a global lock, for now we use an MCS lock.
  static mcs_qnode_t* lock = NULL;
  static __thread mcs_qnode_t node = {0};

  ///
  /// Initialize each thread's node.
  static void __attribute__((constructor))
  init_node() {
      node.flag = false;
      node.next = &node; // our little hack to detect if our node is in the
                         // queue, if node.next != node, then we're waiting
  }
}

///
/// The CGL pre-checkpoint code actually acquires the lock, and determines if we
/// need to make a checkpoint. This is called from the checkpoint-<arch>.S asm,
/// and is responsible for returning a pointer to the checkpoint to use. The asm
/// understands that, if the returned value is NULL, then it should not fill in
/// the checkpoint.
rstm::checkpoint_t* const
rstm::pre_checkpoint(const uint32_t flags) {
    // if this is an outermost pre_checkpoint, then acquire the lock (be polite
    // to darwin, where TLS is emulated with pthreads, and only hit the node
    // once)
    //
    // we use this hack (setting the MCS next pointer to point at "this") to
    // distinguish between a node that's in the list, and a node that
    // isn't. This is because we're using a static for nesting depth. Could use
    // a thread local instead, but we're trying to limit exposure to __thread.
    mcs_qnode_t* mine = &node;
    if (mine->next != mine)
        rstm::acquire(&lock, mine);

    // update the nesting depth
    assert(UINT32_MAX - depth > 1 && "STM nesting depth overflow.");
    ++depth;

    // if this scope has no aborts, then we don't need a checkpoint
    if (flags & pr_hasNoAbort)
        return NULL;

    // otherwise, allocate a checkpoint, and remember where the undo log was
    // positioned so that we can roll back correctly
    scopes.push_back(Scope(depth, undos.size()));
    return &scopes.back().checkpoint;
}

///
/// The CGL post_checkpoint code is called when we actually performed a
/// checkpoint, which by definition means that we're not irrevocable. Respond to
/// the caller that we should run the ininstrumented code, and save any live
/// variables that might be needed.
uint32_t
rstm::post_checkpoint(uint32_t flags, ...) {
    return a_runInstrumentedCode | a_saveLiveVariables;
}

///
/// The CGL post_checkpoint_nested indicates a begin that didn't need a
/// checkpoint---i.e., pr_hasNoAbort was set. If we're irrevocable and there's
/// an uninstrumented path available, take it, otherwise take the instrumented
/// path. If we're not irrevocable, then we have to take the instrumented path
/// as well, but we don't have a checkpoint to jump to, so saving live variables
/// isn't going to make a difference.
uint32_t
rstm::post_checkpoint_nested(uint32_t flags, ...) {
    return (scopes.empty() && (flags & pr_uninstrumentedCode)) ?
    a_runUninstrumentedCode : a_runInstrumentedCode;
}

///
/// Commiting a transaction is _always_ a single level.
void
_ITM_commitTransaction() {
    assert(depth - 1 >= 0 && "Poorly paired transaction begin/end.");

    // if this is an outermost commit, clear our logs and release the lock,
    // again being nice to darwin where tls is emulated with pthreads
    if (--depth == 0) {
        undos.clear();
        scopes.clear();
        mcs_qnode_t* mine = &node;
        rstm::release(&lock, mine);
        mine->next = mine;            // implement our hack see pre-checkpoint
        return;
    }

    // if we're irrevocable, then just return
    if (scopes.empty())
        return;

    // otherwise, if we created a checkpoint at this depth, merge it into its
    // parent's by popping the scope record.
    if (scopes.back().depth == depth + 1)
        scopes.pop_back();
}

///
/// CGL doesn't abort due to conflicts, so this can only happen as a result of a
/// user abort. It could be either a request to abort the inner scope, or the
/// outermost scope. Either way, we must have made a checkpoint for the scope,
/// which is in the scopes stack.
void
_ITM_abortTransaction(_ITM_abortReason reason) {
    assert(reason & userAbort && "Unhandled abort reason");

    // Clear any unnecessary scopes, if we're aborting to the outermost scope
    // (this implicitly commits the nested scopes into the outermost scope,
    // but we'll be rolling hat all back in a second, so there's no issue).
    if (reason & outerAbort)
        scopes.resize(1);

    // pop the scope
    //
    // TODO: this copies the scope, including the checkpoint---maybe we should
    //       use dynamically allocated scopes?
    Scope scope(scopes.back());
    scopes.pop_back();

    // Perform all of the undos up to the scope's index.
    for (int i = undos.size() - 1; i >= scope.index; --i)
        undos[i].undo();

    // reset the undo log
    undos.resize(scope.index);

    // update our depth
    depth = scope.depth;

    // and restore the scope
    rstm::restore_checkpoint(&scope.checkpoint,
                             a_abortTransaction | a_restoreLiveVariables);
}


///
/// The CGL read operation always just returns the value in memory.
#define RSTM_LIBITM_READ(SYMBOL, CALLING_CONVENTION, TYPE)  \
    TYPE SYMBOL(const TYPE* addr) {                         \
        return *addr;                                       \
    }
#include "libitm-dtfns.def"
#undef RSTM_LIBITM_READ

///
/// The CGL write operation needs to log values when we're not irrevocable. Once
/// the adress is logged it can write in place.
///
/// Can't currently optimize for WaW because we don't know what scope we last
/// wrote it at.
/// TODO: check to see if WaW implies the same scope, in which case we can
/// customize a WaW barrier that just does the in-place write.
#define RSTM_LIBITM_WRITE(SYMBOL, CALLING_CONVENTION, TYPE) \
    void SYMBOL(TYPE* address, TYPE value) {                \
        if (!scopes.empty())                                \
            undo_log(address);                              \
        *address = value;                                   \
    }
#include "libitm-dtfns.def"
#undef RSTM_LIBITM_WRITE

/// TODO: We need RSTM_LIBITM_LOG barriers, as well as all of the mem*
/// barriers.

