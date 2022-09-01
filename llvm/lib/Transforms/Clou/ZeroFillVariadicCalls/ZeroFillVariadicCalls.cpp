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

/* Zero-fill all register parameters in variadic and indirect calls.
 * This means zero-extending the parameters to the full register bitwidth (64 on x86_64) and zeroing out unused parameter-passing
 * registers.
 * 
 * Why variadic calls: due to misspeculative control-flow, the variadic callee might try to access an argument in a register that
 * wasn't passed/initialized by the caller. For example, if open(int fd, int flags, ...) gets a stale value of flags with
 * O_CREAT set, then open() will try to use a stale, potentially secret, value in %rdx (the third parameter-passing register on
 * x86_64).
 *
 * Why indirect calls: indirect branch misprediction might cause a different callee with more arguments to be invoked.
 * This misspeculative callee will then access a stale, potentially secret value in one of the unused parameter-passing registers.
 */

using namespace llvm;

namespace {

  constexpr int num_param_regs = 6; // TODO: unify this with other definitions.

  struct ZeroFillCalls final: public FunctionPass {
    static char ID;
    ZeroFillCalls(): FunctionPass(ID) {}

    virtual bool runOnFunction(Function& F) override {
      bool changed = false;

      for (BasicBlock& B : F) {
	for (Instruction& I : B) {
	  if (CallBase *C = dyn_cast<CallBase>(&I)) {
	    FunctionType *T = C->getFunctionType();

	    bool is_variadic = T->isVariadic();
	    bool is_indirect = C->isIndirectCall();

	    IRBuilder<> IRB (C);

	    // get new function type
	    // we will widen the first <= 6 arguments to 64-bits
	    std::vector<Value *> newC_args;
	    for (unsigned i = 0; i < std::min(C->arg_size(), 6); ++i) {
	      Value *argC = C->getArg(i);
	      Type *argT = argC->getType();
	      Value *newArgC;
	      if (isa<PointerType>(argT)) {
		newArgC = argC;
	      } else if (isa<IntegerType>(argT)) {
		newArgT = Type::getInt64Ty(ctx);
		newArgC = IRB.CreateZExt(argC, Type::getInt64Ty(ctx));
	      } else {
		errs() << "CLOU: internal error: " << getPassName() << ": unhandled type: " << *argT << "\n";
		std::abort();
	      }
	      newC_args.push_back(newArgC);
	    }

	    // fill remaining arguments
	    for (unsigned i = 6; i < C->arg_size(); ++i) {
	      newC_args.push_back(C->getArg(i));
	    }

	    // create new function type
	    std::vector<Type *> newT_args;
	    for (Value *V : newC_args) {
	      newT_args.push_back(V->getType());
	    }
	    FunctionType *newT = FunctionType::Create(C->getType(), newT_args, false);
	    
	    for (Type *argT : T->params()) {
	      Type *newArgT;
	      Value *newArgC;
	      if (isa<PointerType>(argT)) {
		newArgT = argT;
		newArgC = IRB.CreateZExt(
	      } else if (isa<IntegerType>
		
	    }

	    // 
	    
	    if (T->isVariadic()) {
	      if (C->arg_size() < num_param_regs) {
		// create new call
		IRBuilder<> IRB (C);
		std::vector<Value *> args;
		for (Use& U : C->args()) {
		  args.push_back(U.get());
		}
		while (args.size() < num_param_regs) {
		  args.push_back(Constant::getNullValue(
		}
		IRB.CreateCall(T, 
	      }
	    }
	  }
	}
      }

      return changed;
    }
  };

  char ZeroFillCalls::ID = 0;
  
}
