#include "X86.h"
#include "X86InstrInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "X86Subtarget.h"
#include "llvm/IR/Module.h"

using namespace llvm;

namespace llvm {

cl::opt<bool> EnableFunctionLocalStacks("x86-function-local-stacks",
					cl::desc("Force enable function-local stacks"),
					cl::init(false)
					);
  
}

namespace {

  class X86FunctionLocalStacks final: public MachineFunctionPass {
  public:
    static char ID;

    X86FunctionLocalStacks(): MachineFunctionPass(ID) {
      initializeX86FunctionLocalStacksPass(*PassRegistry::getPassRegistry());
    }

    virtual bool runOnMachineFunction(MachineFunction& MF) override {
      if (!EnableFunctionLocalStacks) {
	return false;
      }
      
      const TargetInstrInfo *TII = MF.getSubtarget<X86Subtarget>().getInstrInfo();
      const Function& F = MF.getFunction();
      const GlobalValue *sp = MF.getFunction().getParent()->getNamedValue((F.getName() + "_sp").str());
      assert(sp != nullptr);

      for (auto& MBB : MF) {
	for (auto MBBI = MBB.begin(); MBBI != MBB.end(); ++MBBI) {
	  if (MBBI->isCall()) {
	    const auto& DL = MBBI->getDebugLoc();
	    
	    // store SP right before
	    BuildMI(MBB, MBBI, DL, TII->get(X86::MOV64mr))
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

	    const auto MBBI_next = std::next(MBBI);
	    if (MBBI_next != MBB.end()) {
	      // Check if SP is correct -- Spectre RSB hardening
	      // xor rdi, rdi // rdi is dead, clobbered register
	      // cmp rsp, [func_sp]
	      // cmovne rsp, rdi
	      // cmovne rbp, rdi
	      // cmovne rbx, rdi
	      BuildMI(MBB, MBBI_next, DL, TII->get(X86::XOR64rr), X86::RDI)
		.addReg(X86::RDI)
		.addReg(X86::RDI);
#if 1
	      BuildMI(MBB, MBBI_next, DL, TII->get(X86::CMP64rm))
		.addReg(X86::RSP)
		.addReg(X86::RIP)
		.addImm(1)
		.addReg(0)
		.addGlobalAddress(sp, 0)
		.addReg(0)
		.addMemOperand(MBB.getParent()->getMachineMemOperand(MachinePointerInfo(sp),
								     MachineMemOperand::MOStore,
								     8,
								     Align(8)));
#endif
#if 1
	      for (const auto reg : {X86::RSP, X86::RBP, X86::RBX}) {
		BuildMI(MBB, MBBI_next, DL, TII->get(X86::CMOV64rr), reg)
			.addReg(reg)
			.addReg(X86::RDI)
			.addImm(X86::COND_NE);
	      }
#endif
		
	    }
	  }
	}
      }

#if 0
      // set callee-saved regs
      static const MCPhysReg csrs[] = {X86::RBX, X86::R12, X86::R13, X86::R14, X86::R15};
      MF.getRegInfo().setCalleeSavedRegs(csrs);
#endif
      
      return true;
    }
  };

  char X86FunctionLocalStacks::ID = 0;

}

INITIALIZE_PASS(X86FunctionLocalStacks, "x86-function-local-stacks", "X86 Function Local Stacks Pass", false, false)

namespace llvm {

  FunctionPass *createX86FunctionLocalStacksPass() { return new X86FunctionLocalStacks(); }
  
}
