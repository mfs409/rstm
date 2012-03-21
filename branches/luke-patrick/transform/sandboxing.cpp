/**
 *  Copyright (C) 2011
 *  University of Rochester Department of Computer Science
 *    and
 *  Lehigh University Department of Computer Science and Engineering
 *
 * License: Modified BSD
 *          Please see the file LICENSE.RSTM for licensing information
 */

#define DEBUG_TYPE "sandbox"
#include "llvm/Pass.h"
#include "llvm/Instructions.h"
#include "llvm/Module.h"
#include "llvm/Value.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Support/CFG.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/IRBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TypeBuilder.h"
#include "tanger.h"                     // TangerTransform pass
#include <set>
using namespace llvm;
using std::set;

// ----------------------------------------------------------------------------
// Stringification macro.
// ----------------------------------------------------------------------------
#define STRINGIFY(n) #n
#define STRINGIFY2(n) STRINGIFY(n)

// ----------------------------------------------------------------------------
// The type for our validation function. This should appear in the STM library
// archive.
// ----------------------------------------------------------------------------
extern "C" void stm_validation_full(void);

// ----------------------------------------------------------------------------
// Provide some template specializations needed by TypeBuilder. These need to
// live in the llvm namespace.
// ----------------------------------------------------------------------------
namespace llvm {
  template <bool xcompile>
  class TypeBuilder<bool, xcompile> {
    public:
      static const IntegerType* get(LLVMContext& context) {
          return TypeBuilder<types::i<1>, xcompile>::get(context);
      }
  };
}  // namespace llvm


namespace {
  STATISTIC(validations, "Number of stm_validation_full barriers inserted.");
  // --------------------------------------------------------------------------
  // A utility that gets the target function of either a call or invoke
  // instruction.
  // --------------------------------------------------------------------------
  static Function* GetTarget(CallSite call) {
      // Get the called function (NULL if this is an indirect call).
      Function* target = call.getCalledFunction();

      // There's something else to worry about here. We could have a called
      // value that's a pointer cast, in which case we need to strip the
      // pointer casts to see if we can figure out the function target.
      if (!target) {
          Value* v = call.getCalledValue();
          target = dyn_cast<Function>(v->stripPointerCasts());
      }

      return target;
  }

  // --------------------------------------------------------------------------
  // Finds the called function for a call instruction.
  // --------------------------------------------------------------------------
  static Function* GetTarget(CallInst* call) {
      if (!call)
          return NULL;

      // Treat inline asm as an opaque block of code that is equivalent to an
      // indirect call.
      if (call->isInlineAsm())
          return NULL;

      return GetTarget(CallSite(call));
  }

  // --------------------------------------------------------------------------
  // Abstracts a transactional ABI. Pure-virtual superclass so that we can
  // adapt sandboxing quickly to other ABIs.
  // --------------------------------------------------------------------------
  class TransactionRecognizer {
    public:
      virtual ~TransactionRecognizer();

      virtual bool init(Module& m) = 0;

      virtual bool isBeginMarker(Instruction*) const = 0;
      virtual bool isEndMarker(Instruction*) const = 0;
      virtual bool isReadBarrier(Instruction*) const = 0;
      virtual bool isWriteBarrier(Instruction*) const = 0;
      virtual bool isABI(Instruction*) const = 0;
      virtual bool isTransactionalClone(Function*) const = 0;
      virtual Function* getGetTx() const = 0;
  };

  // --------------------------------------------------------------------------
  // Recognizes the tanger-specific ABI.
  // --------------------------------------------------------------------------
  class TangerRecognizer : public TransactionRecognizer {
    public:
      TangerRecognizer();
      ~TangerRecognizer();

      bool init(Module& m);
      bool isBeginMarker(Instruction*) const;
      bool isEndMarker(Instruction*) const;
      bool isReadBarrier(Instruction*) const;
      bool isWriteBarrier(Instruction*) const;
      bool isABI(Instruction*) const;
      bool isTransactionalClone(Function*) const;
      Function* getGetTx() const;

    private:
      Function* get_tx;

      // During initialization we grab pointers to the transactional marker
      // functions that we need. These include the begin and end markers, and
      // the read and write barriers.
      SmallPtrSet<Function*, 2> begins;
      SmallPtrSet<Function*, 2> ends;

      SmallPtrSet<Function*, 16> reads;
      SmallPtrSet<Function*, 16> writes;

      SmallPtrSet<Function*, 8> all;
  };

  // --------------------------------------------------------------------------
  // Hardcode some strings that I need to deal with tanger-transactified code.
  // --------------------------------------------------------------------------
  static const char* clone_prefix = "tanger_txnal_";

  static const char* get_transaction_marker = "tanger_stm_get_tx";

  static const char* begin_transaction_markers[] = {
      "_ITM_beginTransaction"
  };

  static const char* end_transaction_markers[] = {
      "_ITM_commitTransaction"
  };

  static const char* other_abi_markers[] = {
      "tanger_stm_indirect_resolve_multiple",
      "_ITM_malloc",
      "_ITM_free"
  };

  static const char* read_barriers[] = {
      "_ITM_RU1",
      "_ITM_RU2",
      "_ITM_RU4",
      "_ITM_RU8"
  };

  static const char* write_barriers[] = {
      "_ITM_WU1",
      "_ITM_WU2",
      "_ITM_WU4",
      "_ITM_WU8"
  };

  // --------------------------------------------------------------------------
  // Implements the simple sandboxing pass from Transact. Looks for
  // transactionalize functions and top-level transactions to
  // instrument. Assumes that all functions and basic blocks are tainted on
  // entry.
  // --------------------------------------------------------------------------
  struct SRVEPass : public FunctionPass, public TangerRecognizer {
      static char ID;
      SRVEPass();

      bool doInitialization(Module&);
      bool doFinalization(Module&);
      bool runOnFunction(Function&);

    private:
      void visit(BasicBlock*, int);     // recursive traversal
      bool isDangerous(Instruction*) const;

      set<BasicBlock*> blocks;          // used during visit() recursion
      set<Function*> funcs;             // populated with txnly interesting fs
      IRBuilder<>* ir;                  // used to inject instrumentation
      Constant* do_validate;            // the validation function we're using
  };

  char SRVEPass::ID = 0;
  RegisterPass<SRVEPass> S("sandbox-tm", "Sandbox Tanger's Output", false,
                           false);
}

SRVEPass::SRVEPass()
  : FunctionPass(ID), TangerRecognizer(),
    blocks(), funcs(), ir(NULL), do_validate(NULL) {
}

// ----------------------------------------------------------------------------
// Populate the set of functions that we care about (i.e., those that have a
// call to get the transaction descriptor).
// ----------------------------------------------------------------------------
bool
SRVEPass::doInitialization(Module& m) {
    // init() will return false if the TangerRecognizer doesn't find the tanger
    // ABI in the module.
    if (!init(m))
        return false;

    // Find all of the uses of the get_tx ABI call (this appears in all lexical
    // transactions as well as in transactionalized functions).
    Function* f = getGetTx();
    for (Value::use_iterator i = f->use_begin(), e = f->use_end(); i != e; ++i)
    {
        CallInst* call = dyn_cast<CallInst>(*i);
        if (!call)
            report_fatal_error("User of marker is not a call instruction");

        funcs.insert(call->getParent()->getParent());
    }

    if (funcs.size() == 0)
        return false;

    // If we found any functions to transactionalize, then we initialize our
    // instruction builder and inject the validation function into the module.
    ir = new IRBuilder<>(m.getContext());
    do_validate = m.getOrInsertFunction("stm_validation_full",
        TypeBuilder<typeof(stm_validation_full), false>::get(m.getContext()));
    return true;
}

// ----------------------------------------------------------------------------
// Clean up the ir builder that we newed in doInitialization.
// ----------------------------------------------------------------------------
bool
SRVEPass::doFinalization(Module& m) {
    delete ir;
    return false;
}

// ----------------------------------------------------------------------------
// Process a function---called for every function in the module.
// ----------------------------------------------------------------------------
bool
SRVEPass::runOnFunction(Function& f) {
    // do we care about this function?
    if (funcs.find(&f) == funcs.end())
        return false;

    // We're doing a depth-first search, and we check some assumptions about
    // the proper nesting of begin and end transaction markers. Setting the
    // depth to 1 for clones makes the logic work correctly.
    int depth = (isTransactionalClone(&f)) ? 1 : 0;
    DEBUG(if (depth) outs() << "transactional clone: " << f.getName() << "\n");

    // DFS (recursive) of the blocks in the function.
    blocks.clear();
    blocks.insert(&f.getEntryBlock());
    visit(&f.getEntryBlock(), depth);

    return true;
}

// ----------------------------------------------------------------------------
// Manages both the depth-first traversal of blocks, and the instrumentation of
// the block. Doing this DFS is the only way that we know if a block should be
// transactional or not.
// ----------------------------------------------------------------------------
void
SRVEPass::visit(BasicBlock* bb, int depth) {
    // We always assume that a basic block starts tainted.
    bool tainted = true;

    // We want to know if this basic block had a begin-transaction in it,
    // because we want to avoid instrumenting the serial-irrevocable code
    // path, if possible.
    bool had_begin = false;

    // We want to use some domain-specific knowledge to avoid instrumentation
    // on the serial-irrevocable path.

    for (BasicBlock::iterator i = bb->begin(), e = bb->end(); i != e; ++i) {
        // If we are terminating with a return, the depth should be 0 if we're
        // not processing a transactional clone. Otherwise, we're processing a
        // transactional clone and the depth should be 1. This just does error
        // checking, because we believe all returns to be safe (see paper).
        if (isa<ReturnInst>(i)) {
            if (isTransactionalClone(bb->getParent())) {
                if (depth != 1)
                    report_fatal_error("Unmatched transaction begin marker");
            } else if (depth != 0) {
                report_fatal_error("Unmatched transaction begin marker");
            }
        }

        // Begin markers increment our nesting depth. Testing for overflow can
        // help us find analysis loops.
        if (isBeginMarker(i)) {
            DEBUG(outs() << "begin transaction: " << *i << "\n");
            if (INT_MAX - depth < 1)
                report_fatal_error("Nesting error in search (overflow).");
            ++depth;
            had_begin = true;
        }

        // End marker decrements nesting depth. Underflow signifies unmatched
        // end marker along some path.
        if (isEndMarker(i)) {
            DEBUG(outs() << "end transaction: " << *i << "\n");
            if (--depth < 0)
                report_fatal_error("Unbalanced transactional end marker");
        }

        if (depth) {
            // read barriers introduce taint
            if (isReadBarrier(i)) {
                tainted = true;
                continue;
            }

            // other ABI calls are neutral
            if (isABI(i))
                continue;

            // dangerous operations cannot be executed from a potentially
            // tainted context
            if (isDangerous(i)) {
                if (tainted) {
                    ir->SetInsertPoint(i);
                    ir->CreateCall(do_validate);
                    tainted = false;
                    validations++;
                    DEBUG(outs() << " INSTRUMENTED: " << validations << "\n");
                } else {
                    DEBUG(outs() << " SRVE Suppressed.\n");
                }
            }

            // function calls and invokes introduce taint, but only after we
            // have pre-validated them
            if (isa<CallInst>(i) || isa<InvokeInst>(i))
                tainted = true;
        }
    }

    // Special case for blocks with begin transaction instructions---mark the
    // "default" target as visited. This is the serial-irrevocable block for
    // tanger transactions.
    //
    // TODO: We should a) verify this is always the case and b) abstract this
    //       into the TangerRecognizer class.
    if (had_begin) {
        SwitchInst* sw = dyn_cast<SwitchInst>(bb->getTerminator());
        assert(sw && "Expected a _ITM_beginTransaction block to terminate "
                      "with a switch");
        DEBUG(outs() << "eliding serial-irrevocable instrumentation\n");
        blocks.insert(sw->getDefaultDest());
    }

    // Done this block, continue depth first search.
    for (succ_iterator bbn = succ_begin(bb), e = succ_end(bb); bbn != e; ++bbn)
        if (blocks.insert(*bbn).second)
            visit(*bbn, depth);
}

// ----------------------------------------------------------------------------
// Encodes instruction types that we consider dangerous.
// ----------------------------------------------------------------------------
bool
SRVEPass::isDangerous(Instruction* i) const {
    // stores are always dangerous
    if (isa<StoreInst>(i)) {
        DEBUG(outs() << "dangerous store: " << *i << "... ");
        return true;
    }

    if (isa<LoadInst>(i)) {
        DEBUG(outs() << "dangerous load: " << *i << "... ELIDED\n");
        return false;
    }

    // dynamically sized allocas are dangerous
    if (AllocaInst* a = dyn_cast<AllocaInst>(i)) {
        if (a->isArrayAllocation() && !isa<Constant>(a->getArraySize())) {
            DEBUG(outs() << "dangerous alloca: " << *i << "... ");
            return true;
        }
    }

    // Indirect calls and invokes are *not* dangerous, because the tanger
    // mapping instrumentation already does a check to see if the target is
    // transactional, and goes serial irrevocable (hence validates) if it
    // isn't.

    if (CallInst* call = dyn_cast<CallInst>(i)) {
        if (call->isInlineAsm()) {
            DEBUG(outs() << "dangerous inline asm: " << *i << "... ");
            return true;
        }

        if (!GetTarget(call)) {
            // DEBUG(outs() << "indirect call: " << *i << "... ");
            // return true;
            DEBUG(outs() << "indirect call: " << *i << "... ELIDED\n");
            return false;
        }
    }

    if (InvokeInst* invoke = dyn_cast<InvokeInst>(i)) {
        if (!GetTarget(invoke)) {
            // DEBUG(outs() << "indirect call: " << *i << "... ");
            // return true;
            DEBUG(outs() << "indirect call: " << *i << "... ELIDED\n");
            return false;
        }
    }

    // Used to implement switches. Right now we consider these dangerous.
    if (dyn_cast<IndirectBrInst>(i)) {
        DEBUG(outs() << "dangerous indirect branch: " << *i << "... ");
        return true;
    }

    return false;
}

TransactionRecognizer::~TransactionRecognizer() {
}

TangerRecognizer::TangerRecognizer()
  : TransactionRecognizer(),
    get_tx(NULL), begins(), ends(), reads(), writes(), all() {
}

TangerRecognizer::~TangerRecognizer() {
}

bool
TangerRecognizer::init(Module& m) {
    // Check to see if there are any transactions in the module. We do this
    // using the get_transaction_marker.
    get_tx = m.getFunction(get_transaction_marker);
    if (!get_tx)
        return false;
    all.insert(get_tx);

    // Find the begin markers.
    for (int i = 0, e = array_lengthof(begin_transaction_markers); i < e; ++i) {
        if (Function* begin = m.getFunction(begin_transaction_markers[i])) {
        begins.insert(begin);
        all.insert(begin);
        }
    }

    // Find the end markers.
    for (int i = 0, e = array_lengthof(end_transaction_markers); i < e; ++i) {
        if (Function* end = m.getFunction(end_transaction_markers[i])) {
        ends.insert(end);
        all.insert(end);
    }
    }

    // Find the read barriers that are used in the module.
    for (int i = 0, e = array_lengthof(write_barriers); i < e; ++i) {
        if (Function* read = m.getFunction(read_barriers[i])) {
            reads.insert(read);
            all.insert(read);
        }
    }

    // Find the write barriers that are used in the module.
    for (int i = 0, e = array_lengthof(read_barriers); i < e; ++i) {
        if (Function* write = m.getFunction(write_barriers[i])) {
            writes.insert(write);
            all.insert(write);
        }
    }

    // Find markers that we don't care about.
    for (int i = 0, e = array_lengthof(other_abi_markers); i < e; ++i) {
        if (Function* f = m.getFunction(other_abi_markers[i])) {
            all.insert(f);
        }
    }

    return true;
}

bool
TangerRecognizer::isBeginMarker(Instruction* i) const {
    return begins.count(GetTarget(dyn_cast<CallInst>(i)));
}

bool
TangerRecognizer::isEndMarker(Instruction* i) const {
    return ends.count(GetTarget(dyn_cast<CallInst>(i)));
}

bool
TangerRecognizer::isReadBarrier(llvm::Instruction* i) const{
    return reads.count(GetTarget(dyn_cast<CallInst>(i)));
}

bool
TangerRecognizer::isWriteBarrier(llvm::Instruction* i) const {
    return writes.count(GetTarget(dyn_cast<CallInst>(i)));
}

bool
TangerRecognizer::isABI(llvm::Instruction* i) const {
    return all.count(GetTarget(dyn_cast<CallInst>(i)));
}

bool
TangerRecognizer::isTransactionalClone(Function* f) const {
    return f->getName().startswith(clone_prefix);
}

Function*
TangerRecognizer::getGetTx() const {
    return get_tx;
}
