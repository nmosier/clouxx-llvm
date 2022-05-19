#include "llvm/IR/IRBuilder.h"
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

constexpr const char *sep = "_";

namespace {

  constexpr uint64_t stack_size = 0x20000;
  const Align max_align (256);

  struct FunctionLocalStacks final: public ModulePass {
    static char ID;

    FunctionLocalStacks(): ModulePass(ID) {}

    virtual bool runOnModule(Module& M) override {
      std::vector<GlobalVariable *> GVs;
      
      for (Function& F : M) {
	if (!F.isDeclaration()) {
	  runOnFunction(F, std::back_inserter(GVs));
	}
      }

      return true;
    }

    template <class OutputIt>
    void runOnFunction(Function& F, OutputIt out) {
      F.addFnAttr(Attribute::get(F.getContext(), "stackrealign"));
      
      Module& M = *F.getParent();

      // Type:
      Type *stack_ty = ArrayType::get(IntegerType::getInt8Ty(F.getContext()), stack_size);
      Type *sp_ty = PointerType::get(IntegerType::getInt8Ty(F.getContext()), 0);
      const auto stack_name = (F.getName() + sep + "stack").str();
      const auto sp_name = (F.getName() + sep + "sp").str();

      // determine correct linkage
      GlobalVariable::LinkageTypes linkage = F.getLinkage();

      GlobalVariable *stack = new GlobalVariable(M, stack_ty, false, linkage, Constant::getNullValue(stack_ty), stack_name, nullptr, GlobalValue::NotThreadLocal);
      GlobalVariable *sp = new GlobalVariable(M,
					      sp_ty,
					      false,
					      linkage,
					      ConstantExpr::getBitCast(ConstantExpr::getGetElementPtr(stack_ty, stack,
												      Constant::getIntegerValue(Type::getInt8Ty(M.getContext()),
																APInt(8, 1))),
								       sp_ty),
					      sp_name, nullptr, GlobalValue::NotThreadLocal);
      stack->setDSOLocal(true);
      sp->setDSOLocal(true);
      stack->setAlignment(max_align);
      sp->setAlignment(Align(8)); // TODO: actually compute size of pointer?

      *out++ = stack;
      *out++ = sp;

      // Save old function-local stack pointer on entry + exit
      LoadInst *LI = IRBuilder<>(&F.getEntryBlock().front()).CreateLoad(sp_ty, sp);
      for (BasicBlock &B : F) {
	Instruction *I = &B.back();
	if (isa<ReturnInst>(I)) {
	  IRBuilder<>(I).CreateStore(LI, sp);
	}
      }
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
static RegisterStandardPasses Z (PassManagerBuilder::/*EP_ModuleOptimizerEarly*/EP_OptimizerLast, reg);
