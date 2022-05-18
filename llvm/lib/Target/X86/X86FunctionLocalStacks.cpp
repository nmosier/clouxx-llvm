#include "X86.h"
#include "X86InstrInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "X86Subtarget.h"
#include "llvm/IR/Module.h"

using namespace llvm;

namespace {

  class X86FunctionLocalStacks final: public MachineFunctionPass {
  public:
    static char ID;

    X86FunctionLocalStacks(): MachineFunctionPass(ID) {
      initializeX86FunctionLocalStacksPass(*PassRegistry::getPassRegistry());
    }

    virtual bool runOnMachineFunction(MachineFunction& MF) override {
      const TargetInstrInfo *TII = MF.getSubtarget<X86Subtarget>().getInstrInfo();
      const Function& F = MF.getFunction();
      const GlobalValue *sp = MF.getFunction().getParent()->getNamedValue((F.getName() + "_sp").str());
      assert(sp != nullptr);

      for (auto& MBB : MF) {
	for (auto MBBI = MBB.begin(); MBBI != MBB.end(); ++MBBI) {
	  if (MBBI->isCall()) {
	    // store SP right before
	    auto tmp = BuildMI(MBB, MBBI, MBBI->getDebugLoc(), TII->get(X86::MOV64mr))
	      .addReg(X86::RIP)
	      .addImm(1)
	      .addReg(0)
	      .addGlobalAddress(sp, 0)
	      .addReg(0)
	      .addMemOperand(MBB.getParent()->getMachineMemOperand(MachinePointerInfo(sp),
								   MachineMemOperand::MOStore,
								   8,
								   Align(8)))
	      .addReg(X86::RSP);
	  }
	}
      }

      // set callee-saved regs
      static const MCPhysReg csrs[] = {X86::RBX, X86::R12, X86::R13, X86::R14, X86::R15};
      MF.getRegInfo().setCalleeSavedRegs(csrs);
      errs() << "set callee-saved regs\n\n";
      
      return true;
    }
  };

  char X86FunctionLocalStacks::ID = 0;

}

INITIALIZE_PASS(X86FunctionLocalStacks, "x86-function-local-stacks", "X86 Function Local Stacks Pass", true, true);

namespace llvm {

  FunctionPass *createX86FunctionLocalStacksPass() { return new X86FunctionLocalStacks(); }
  
}
