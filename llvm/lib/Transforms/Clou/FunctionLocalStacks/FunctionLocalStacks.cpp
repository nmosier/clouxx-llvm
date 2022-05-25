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

constexpr const char *sep = "_"; // TODO: get rid of

namespace {

  constexpr uint64_t stack_size = 0x20000; // TODO: make these command-line parameters
  const Align max_align (256); // TODO: make command-line parameter

  struct FunctionLocalStacks final: public ModulePass {
    static char ID;

    FunctionLocalStacks(): ModulePass(ID) {}

    virtual bool runOnModule(Module& M) override {
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

      replaceMalloc(M);
      replaceMemoryRealloc(M, "realloc");
      replaceMemoryRealloc(M, "reallocarray");
      
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

    CallInst *findCallToFunction(Function& F, StringRef name) {
      for (BasicBlock& B : F) {
	for (Instruction& I : B) {
	  if (CallInst *C = dyn_cast<CallInst>(&I)) {
	    if (Function *callee = C->getCalledFunction()) {
	      if (callee->getName() == name) {
		return C;
	      }
	    }
	  }
	}
      }
      return nullptr;
    }

    void replaceMalloc(Module& M) {
      auto& ctx = M.getContext();
      if (Function *F = M.getFunction("__clou_wrap_malloc")) {
	// find call instruction
	for (BasicBlock& B : *F) {
	  for (Instruction& I : B) {
	    if (CallInst *C = dyn_cast<CallInst>(&I)) {
	      if (Function *callee = C->getCalledFunction()) {
		if (callee->getName() == "malloc") {
		  Type *I64 = Type::getInt64Ty(ctx);
		
		  // get declaration of calloc
		  Function *callocF = M.getFunction("calloc");
		  if (callocF == nullptr) {
		    std::vector<Type *> args = {I64, I64};
		    FunctionType *callocT = FunctionType::get(Type::getInt8PtrTy(ctx), args, false);
		    callocF = Function::Create(callocT, Function::LinkageTypes::ExternalLinkage, "calloc", M);
		  }


		  IRBuilder<> IRB (C);
		  std::vector<Value *> args = {F->getArg(0), ConstantInt::get(I64, 1)};
		  CallInst *newC = IRB.CreateCall(callocF, args);
		  for (User *U : C->users()) {
		    U->replaceUsesOfWith(C, newC);
		  }
		  C->eraseFromParent();

		  return;
		}
	      }
	    }
	  }
	}
      }

    }

    Function *getMallocUsableSizeDecl(Module& M) {
      StringRef name = "malloc_usable_size";
      if (Function *F = M.getFunction(name)) {
	return F;
      } else {
	LLVMContext& ctx = M.getContext();
	FunctionType *T = FunctionType::get(Type::getInt64Ty(ctx), std::vector<Type *> {Type::getInt8PtrTy(ctx)}, false);
	return Function::Create(T, Function::ExternalLinkage, name, M);
      }
    }

    void replaceMemoryRealloc(Function& wrapF, StringRef realName) {
      LLVMContext& ctx = wrapF.getContext();
      Module& M = *wrapF.getParent();
      CallInst *reallocPtr = findCallToFunction(wrapF, realName);
      assert(reallocPtr != nullptr);
      IRBuilder<> preIRB (reallocPtr);
      IRBuilder<> postIRB (reallocPtr->getNextNode());
      Function *mallocUsableSizeF = getMallocUsableSizeDecl(M);

      CallInst *oldSize =  preIRB.CreateCall(mallocUsableSizeF, std::vector<Value *> {wrapF.getArg(0)});
      CallInst *newSize = postIRB.CreateCall(mallocUsableSizeF, std::vector<Value *> {reallocPtr});
      Value *sizeCmp    = postIRB.CreateICmpULT(oldSize, newSize);
      Value *sizeDiff   = postIRB.CreateSub(newSize, oldSize);
      Value *sizeDiffOrZero = postIRB.CreateSelect(sizeCmp, sizeDiff, Constant::getNullValue(Type::getInt64Ty(ctx)));
      Value *gep = postIRB.CreateInBoundsGEP(reallocPtr->getType()->getPointerElementType(), reallocPtr,
					     std::vector<Value *> {oldSize});
      postIRB.CreateMemSet(gep, Constant::getNullValue(Type::getInt8Ty(ctx)), sizeDiffOrZero, MaybeAlign(16));
    }

    void replaceMemoryRealloc(Module& M, StringRef realName) {
      const Twine wrapName = wrapperName(realName);
      if (Function *F = M.getFunction(wrapName.str())) {
	replaceMemoryRealloc(*F, realName);
      }
    }
      
    InlineAsm *makeFence(LLVMContext& ctx) {
      return InlineAsm::get(FunctionType::get(Type::getVoidTy(ctx), false), "lfence", "",
			    false, InlineAsm::AD_Intel);
    }

    Twine wrapperName(StringRef name) {
      return "__clou_wrap_" + name;
    }

    Twine wrapperName(Function& F) {
      return wrapperName(F.getName());
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

      replaceUses(&F, newF);
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

    void replaceUses(Function *oldF, Function *newF) {
      for (Use& U : oldF->uses()) {
	User *user = U.getUser();

	if (Instruction *I = dyn_cast<Instruction>(user)) {
	  if (I->getFunction() == newF) {
	    continue;
	  }
	}

	if (Constant *C = dyn_cast<Constant>(user)) {
	  C->handleOperandChange(oldF, newF);
	} else {
	  user->replaceUsesOfWith(oldF, newF);
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
