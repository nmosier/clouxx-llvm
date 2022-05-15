#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

using namespace llvm;

namespace {
  struct FunctionLocalStacks final: public FunctionPass {
    static char ID;

    FunctionLocalStacks(): FunctionPass(ID) {}
			   
    virtual bool runOnFunction(Function& F) override {
      errs() << F.getName() << "\n";
      return false;
    }
  };
}

char FunctionLocalStacks::ID = 0;

static RegisterPass<FunctionLocalStacks> X ("function-local-stacks", "Function Local Stacks", false, false);

static RegisterStandardPasses Y (PassManagerBuilder::EP_EarlyAsPossible, [] (const PassManagerBuilder&, legacy::PassManagerBase& PM) {
									   PM.add(new FunctionLocalStacks());
									 });
