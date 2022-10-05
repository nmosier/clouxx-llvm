#include "X86.h"
#include "X86InstrInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "X86Subtarget.h"
#include "llvm/IR/Module.h"
#include "X86FunctionLocalStacks.h"

using namespace llvm;

namespace {

  class X86ReloadSP final : public MachineFunctionPass {
  public:
    static char ID;

    X86ReloadSP(): MachineFunctionPass(ID) {
      initializeX86ReloadSPPass(*PassRegistry::getPassRegistry());
    }

    bool runOnMachineFunction(MachineFunction& MF) override {
      if (!EnableFunctionLocalStacks) {
	return false;
      }

      const TargetInstrInfo *TII = MF.getSubtarget<X86Subtarget>().getInstrInfo();
      const Function& F = MF.getFunction();
      const GlobalValue *sp = MF.getFunction().getParent()->getNamedValue((F.getName() + "_sp").str());
      assert(sp != nullptr);

      for (auto& MBB : MF) {
	for (auto MBBI = MBB.begin(); MBBI != MBB.end(); ++MBBI) {
	  if (MBBI->isCall() && std::next(MBBI) != MBB.end()) {
	    // store SP right before
	    BuildMI(MBB, MBBI, MBBI->getDebugLoc(), TII->get(X86::MOV64mr))
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

	    // store SP right after
	    const auto MBBI_next = std::next(MBBI);
	    if (MBBI_next == MBB.end()) {
	      errs() << *MBBI << "\n";
	    }
	    assert(MBBI_next != MBB.end());
	    BuildMI(MBB, MBBI_next, MBBI_next->getDebugLoc(), TII->get(X86::MOV64rm))
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
	  }
	}
      }

      return true;
    }
  };

  char X86ReloadSP::ID = 0;

}

INITIALIZE_PASS(X86ReloadSP, "x86-reload-sp", "X86 Reload SP Pass", false, false)

namespace llvm {
  FunctionPass *createX86ReloadSPPass() {
    return new X86ReloadSP();
  }
}
