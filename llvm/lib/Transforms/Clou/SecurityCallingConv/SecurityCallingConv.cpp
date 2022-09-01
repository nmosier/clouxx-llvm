#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/IR/InlineAsm.h"

using namespace llvm;

namespace {

  constexpr size_t num_arg_regs = 6;

  struct SecurityCallingConv final: public FunctionPass {
    static char ID;
    SecurityCallingConv(): FunctionPass(ID) {}

    int num_fences;

    virtual bool runOnFunction(Function& F) override {
      bool changed = false;

      // check number of arguments
      if (F.isVarArg() || F.arg_size() > num_arg_regs) {
	// fence on entry
	LLVMContext& ctx = F.getContext();
	Instruction *I = &F.getEntryBlock().front();
	IRBuilder<> IRB (I);
	InlineAsm *fence = InlineAsm::get(FunctionType::get(Type::getVoidTy(ctx), false), "lfence", "", false, InlineAsm::AD_Intel);
	IRB.CreateCall(fence);
	changed = true;
	++num_fences;
      }

      return changed;
    }

    virtual bool doInitialization(Module&) override {
      num_fences = 0;
      return false;
    }
    
    virtual bool doFinalization(Module&) override {
      errs() << getPassName() << ": inserted " << num_fences << " fences\n";
      return false;
    }
  };

  char SecurityCallingConv::ID = 0;

  RegisterPass<SecurityCallingConv> X ("security-calling-conv", "Security Calling Convention", false, false);

  void reg(const PassManagerBuilder&, legacy::PassManagerBase& PM) {
    PM.add(new SecurityCallingConv());
  }

  RegisterStandardPasses Y (PassManagerBuilder::EP_EnabledOnOptLevel0, reg);
  RegisterStandardPasses Z (PassManagerBuilder::EP_OptimizerLast, reg);
}
