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

  struct FunctionAttrs final: public FunctionPass {
    static char ID;
    FunctionAttrs(): FunctionPass(ID) {}

    virtual bool runOnFunction(Function& F) override {
      F.addFnAttr("no-jump-tables");

      return true;
    }
  };

  char FunctionAttrs::ID = 0;

  RegisterPass<FunctionAttrs> X ("clou-function-attrs", "Clou Function Attributes", false, false);

  void reg(const PassManagerBuilder&, legacy::PassManagerBase& PM) {
    PM.add(new FunctionAttrs());
  }

  RegisterStandardPasses Y (PassManagerBuilder::EP_EnabledOnOptLevel0, reg);
  RegisterStandardPasses Z (PassManagerBuilder::EP_OptimizerLast, reg);

}
