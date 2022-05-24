#include <memory>

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
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/IntrinsicInst.h"

using namespace llvm;

constexpr const char *sep = "_";

namespace {

  constexpr uint64_t stack_size = 0x20000;
  const Align max_align (256);

  struct FunctionLocalStacks final: public ModulePass {
    static char ID;

    FunctionLocalStacks(): ModulePass(ID) {}

    virtual bool runOnModule(Module& M) override {
#if 0
      Function *oldMalloc;
      Function *newMalloc;
      emitNewMalloc(M, &oldMalloc, &newMalloc);
      replaceMalloc(oldMalloc, newMalloc);
#endif

      // for every function declaration, emit tentative wrappers
      {
	std::vector<Function *> todo;
	std::transform(M.begin(), M.end(), std::back_inserter(todo), [] (Function& F) { return &F; });
	for (Function *F : todo) {
	  if (F->isDeclaration()) {
	    emitWrapperForDeclaration(*F);
	  } else {
	    emitAliasForDefinition(*F);
	  }
	}
      }
      
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
      GlobalVariable::LinkageTypes linkage = GlobalVariable::LinkageTypes::InternalLinkage;

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

    void emitNewMalloc(Module& M, Function **oldMallocP, Function **newMallocP) {
      auto& ctx = M.getContext();
      
      Function *oldMalloc = M.getFunction("malloc");
      if (oldMalloc == nullptr) {
	std::vector<Type *> types = {Type::getInt64Ty(ctx)};
	FunctionType *mallocTy = FunctionType::get(Type::getInt8PtrTy(ctx), types, false);

	// Declare malloc
	oldMalloc = Function::Create(mallocTy, Function::LinkageTypes::ExternalLinkage, "malloc", M);
      }
      assert(oldMalloc);
      assert(oldMalloc->getType());
      assert(oldMalloc->getType()->getPointerElementType());
      FunctionType *mallocTy = cast<FunctionType>(oldMalloc->getType()->getPointerElementType());
      Function *newMalloc = Function::Create(mallocTy, Function::LinkageTypes::WeakAnyLinkage, "malloc_clou", M);

      BasicBlock *B = BasicBlock::Create(ctx, "", newMalloc);
      IRBuilder<> IRB (B, B->begin());

      // fence
      InlineAsm *IA = InlineAsm::get(FunctionType::get(Type::getVoidTy(ctx), false), "lfence", "", false, InlineAsm::AD_Intel);
      IRB.CreateCall(IA);

      // call to old malloc
      std::vector<Value *> args;
      for (Argument& A : newMalloc->args()) {
	args.push_back(&A);
      }
      Instruction *C = IRB.CreateCall(mallocTy, oldMalloc, args);

      // return
      IRB.CreateRet(C);

      *oldMallocP = oldMalloc;
      *newMallocP = newMalloc;
    }

    void replaceMalloc(Function *oldMalloc, Function *newMalloc) {
      for (Use& U : oldMalloc->uses()) {
	User *user = U.getUser();

	// Don't replace if is the call to old malloc in new malloc!
	if (Instruction *I = dyn_cast<Instruction>(user)) {
	  if (I->getFunction() == newMalloc) {
	    continue;
	  }
	}

	if (Constant *C = dyn_cast<Constant>(user)) {
	  C->handleOperandChange(oldMalloc, newMalloc);
	} else {
	  user->replaceUsesOfWith(oldMalloc, newMalloc);
	}
      }
    }

    InlineAsm *makeFence(LLVMContext& ctx) {
      return InlineAsm::get(FunctionType::get(Type::getVoidTy(ctx), false), "lfence", "",
			    false, InlineAsm::AD_Intel);
    }

    Twine wrapperName(Function& F) {
      return "__clou_wrap_" + F.getName();
    }

    void emitWrapperForDeclaration(Function& F) {
      if (F.isIntrinsic()) {
	return;
      }

      Module& M = *F.getParent();
      auto& ctx = M.getContext();

      // emit weak function definition __clou_<funcname>
      FunctionType *T = cast<FunctionType>(F.getType()->getPointerElementType());
      Function *newF = Function::Create(T, Function::LinkageTypes::WeakAnyLinkage, wrapperName(F), M);
      BasicBlock *B = BasicBlock::Create(ctx, "", newF);
      IRBuilder<> IRB (B, B->begin());

      // entering fence
      IRB.CreateCall(makeFence(ctx));

      // call to external function
      std::vector<Value *> args;
      for (Argument& A : newF->args()) {
	args.push_back(&A);
      }
      Instruction *C = IRB.CreateCall(T, &F, args);
      
      // exiting fence
      IRB.CreateCall(makeFence(ctx));

      // return
      if (C->getType()->isVoidTy()) {
	IRB.CreateRetVoid();
      } else {
	IRB.CreateRet(C);
      }
    }

    void emitAliasForDefinition(Function& F) {
      using L = GlobalAlias::LinkageTypes;
      switch (F.getLinkage()) {
      case L::ExternalLinkage:
	break;
	
      case L::InternalLinkage:
	return;

      default:
	errs() << getPassName() << ": " << __FUNCTION__ << ": unhandled linkage type: " << F.getLinkage() << " for function " << F.getName() << "\n";
	std::abort();
      }
      
      GlobalAlias::create(L::ExternalLinkage, wrapperName(F), &F);
      assert(F.getParent()->getNamedAlias(wrapperName(F).str()) != nullptr);
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
