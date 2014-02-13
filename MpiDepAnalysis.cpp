#define DEBUG_TYPE "MpiDepAnalysis"

#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Pass.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CFG.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/ADT/SmallPtrSet.h"

using namespace llvm;

STATISTIC(SendCounter, "Number of send functions");
STATISTIC(RecvCounter, "Number of receive functions");
STATISTIC(SendBasicBlockCounter, "Number of basic blocks that contain a send");
STATISTIC(RecvBasicBlockCounter, "Number of basic blocks that contain a receive");

// Emulate nullptr unless we're in C++11 mode
#if __cplusplus < 201103L

// Source: SC22/WG21/N2431 = J16/07-0301
// http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2007/n2431.pdf

const                           // this is a const object...
class {
public:
  template<class T>             // convertible to any type
  operator T*() const           // of null non-member
  { return 0; }                 // pointer...

  template<class C, class T>    // or any type of null
  operator T C::*() const       // member pointer...
  { return 0; }
private:
  void operator&() const;       // whose address can't be taken
} nullptr = {};                 // and whose name is nullptr

#endif

namespace {

/// Transitive closure of control dependencies on MPI send/recv calls

/// Find memory allocas that directly or indirectly determine whether an
/// MPI send or receive can execute or not. Currently, the analysis only
/// works for simple memory stores and loads -- no alias analysis is done.

// Implementation note: Passes such as MemoryDependencyAnalysis are not
// const-friendly and so no attempt is made to enforce stronger constness
// rules that would otherwise be applicable (e.g. const Instruction * const).
class MpiDepAnalysis : public ModulePass {
private:
  const std::string SendName;
  const std::string RecvName;

  // 
  typedef SmallPtrSet<BasicBlock*, 256> BasicBlockPtrs;
  BasicBlockPtrs BasicDeps;

  typedef SmallPtrSet<Instruction*, 512> InstructionPtrs;
  InstructionPtrs Deps;
  std::vector<Instruction*> Worklist;

  void initSendRecv(Module &M, Function* &Send, Function* &Recv) {
    Send = nullptr;
    Recv = nullptr;

    for (Module::iterator F = M.begin(), e = M.end(); F != e; ++F) {
      if (SendName == F->getName()) {
        assert(Send == nullptr);
        Send = F;
        DEBUG((errs() << "Send function: ").write_escaped(F->getName()) << '\n');
      } else if (RecvName == F->getName()) {
        assert(Recv == nullptr);
        Recv = F;
        DEBUG((errs() << "Recv function: ").write_escaped(F->getName()) << '\n');
      } else {
        DEBUG((errs() << "Ignore function: ").write_escaped(F->getName()) << '\n');
      }
    }

    assert(Send != nullptr);
    assert(Recv != nullptr);
  }

  void initBasicDeps(Function* Send, Function* Recv) {
    assert(Send != nullptr);
    assert(Recv != nullptr);

    for (Value::use_iterator i = Send->use_begin(), e = Send->use_end(); i != e; ++i) {
      if (Instruction *Inst = dyn_cast<Instruction>(*i)) {
        if (BasicDeps.insert(Inst->getParent()))
          SendBasicBlockCounter++;

        SendCounter++;
      }
    }

    for (Value::use_iterator i = Recv->use_begin(), e = Recv->use_end(); i != e; ++i) {
      if (Instruction *Inst = dyn_cast<Instruction>(*i)) {
        if (BasicDeps.insert(Inst->getParent()))
          RecvBasicBlockCounter++;

        RecvCounter++;
      }
    }
  }

  void initWorklist() {
    for (BasicBlockPtrs::iterator i = BasicDeps.begin(), e = BasicDeps.end(); i != e; ++i) {
      BasicBlock* BB = *i;
      for (pred_iterator PI = pred_begin(BB), e = pred_end(BB); PI != e; ++PI) {
        BasicBlock *Pred = *PI;
        TerminatorInst* TI = Pred->getTerminator();

        for (User::op_iterator op = TI->op_begin(), e = TI->op_end(); op != e; ++op) {
          if (Instruction *U = dyn_cast<Instruction>(*op)) {
            Worklist.push_back(U);
            DEBUG((errs() << "Terminator use-def: ").write_escaped(U->getName()) << '\n');
          }
        }
      }
    }
  }

  void reachFixpoint() {
    while(!Worklist.empty()) {
      Instruction* V = Worklist.back();
      Worklist.pop_back();

      DEBUG((errs() << "Dep: ").write_escaped(V->getName()) << '\n');
      for (User::op_iterator op = V->op_begin(), e = V->op_end(); op != e; ++op) {
        if (Instruction *U = dyn_cast<Instruction>(*op)) {
          DEBUG((errs() << "Dep use-def: ").write_escaped(U->getName()) << '\n');
          Worklist.push_back(U);
        }
      }

      if (Deps.insert(V)) {
        if (AllocaInst *A = dyn_cast<AllocaInst>(V)) {
          for (Value::use_iterator i = A->use_begin(), e = A->use_end(); i != e; ++i) {
            if (Instruction *U = dyn_cast<Instruction>(*i)) {
              DEBUG((errs() << "Dep def-use: ").write_escaped(U->getName()) << '\n');
              Worklist.push_back(U);
            }
          }
        }
      }
    }
  }

  // Analysis result, i.e. transitive closure of control dependencies on MPI calls
  void printAllocaDeps() {
    for (InstructionPtrs::iterator i = Deps.begin(), e = Deps.end(); i != e; ++i) {
      if (AllocaInst *A = dyn_cast<AllocaInst>(*i)) {
        DEBUG((errs() << "Alloca dep: ").write_escaped(A->getName()) << '\n');
      }
    }
  }

  virtual bool runOnModule(Module &M) {
    Function* Send;
    Function* Recv;

    initSendRecv(M, Send, Recv);
    initBasicDeps(Send, Recv);
    initWorklist();
    reachFixpoint();

    DEBUG(printAllocaDeps());

    return false;
  }

  // We don't modify the program, so we preserve all analyses.
  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.setPreservesAll();
  }

public:
  static char ID;

  MpiDepAnalysis()
  : ModulePass(ID),
    SendName("MPI_Send"),
    RecvName("MPI_Recv"),
    BasicDeps(),
    Deps(),
    Worklist() {}

  /// Name of C functions, no attempt is made to support C++ name mangling
  MpiDepAnalysis(
    const std::string& _SendName,
    const std::string& _RecvName)
  : ModulePass(ID),
    SendName(_SendName),
    RecvName(_RecvName),
    BasicDeps(),
    Deps(),
    Worklist() {}
};

}

char MpiDepAnalysis::ID = 0;
static RegisterPass<MpiDepAnalysis> X("mpidep", "MpiDepAnalysis");
