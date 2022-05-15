#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Constant.h"

using namespace llvm;

namespace {

  constexpr uint64_t stack_size = 4096;
  const Align max_align (256);

  struct FunctionLocalStacks final: public ModulePass {
    static char ID;

    FunctionLocalStacks(): ModulePass(ID) {}

    virtual bool runOnModule(Module& M) override {
      for (Function& F : M) {
	if (!F.isDeclaration()) {
	  runOnFunction(F);
	}
      }
      return true;
    }
			   
    bool runOnFunction(Function& F) {
      Module& M = *F.getParent();

      // Type:
      Type *stack_ty = ArrayType::get(IntegerType::getInt8Ty(F.getContext()), stack_size);
      Type *sp_ty = PointerType::get(IntegerType::getInt8Ty(F.getContext()), 0);
      const auto stack_name = (F.getName() + ".stack").str();
      const auto sp_name = (F.getName() + ".sp").str();

      GlobalVariable *stack = new GlobalVariable(M, stack_ty, false, GlobalVariable::InternalLinkage, Constant::getNullValue(stack_ty), stack_name, nullptr, GlobalValue::GeneralDynamicTLSModel);
      GlobalVariable *sp = new GlobalVariable(M, sp_ty, false, GlobalVariable::InternalLinkage, ConstantExpr::getBitCast(stack, sp_ty), sp_name, nullptr, GlobalValue::GeneralDynamicTLSModel);
      stack->setDSOLocal(true);
      sp->setDSOLocal(true);
      stack->setAlignment(max_align);
      sp->setAlignment(Align(8)); // TODO: actually compute size of pointer?

      errs() << F.getName() << "\n";
      
      return true;
    }
  };
}

char FunctionLocalStacks::ID = 0;

static RegisterPass<FunctionLocalStacks> X ("function-local-stacks", "Function Local Stacks", false, false);

namespace {
  void reg(const PassManagerBuilder&, legacy::PassManagerBase& PM) {
    PM.add(new FunctionLocalStacks());
  }
}

static RegisterStandardPasses Y (PassManagerBuilder::EP_EnabledOnOptLevel0, reg);
static RegisterStandardPasses Z (PassManagerBuilder::EP_ModuleOptimizerEarly, reg);
