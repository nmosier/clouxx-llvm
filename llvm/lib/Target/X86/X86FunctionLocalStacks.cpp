#include "X86.h"
#include "X86InstrInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "X86Subtarget.h"
#include "llvm/IR/Module.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/Clou/Clou.h"
#include "llvm/ADT/Statistic.h"

using namespace llvm;

#define DEBUG_TYPE "x86-fps"
STATISTIC(NumMitigations, "Number of mitigations");
STATISTIC(NumInstructions, "Number of instructions inserted");

namespace {

  class X86FunctionLocalStacks final: public MachineFunctionPass {
  public:
    static char ID;

    X86FunctionLocalStacks(): MachineFunctionPass(ID) {
      initializeX86FunctionLocalStacksPass(*PassRegistry::getPassRegistry());
    }

    static bool callUsesRegister(MachineInstr& MI, MCPhysReg reg, const TargetRegisterInfo& TRI) {
      return any_of(concat<MachineOperand>(MI.operands(), MI.implicit_operands()), [&] (const MachineOperand& MO) {
	return MO.isReg() && TRI.regsOverlap(reg, MO.getReg());
      });
    }

    bool runOnMachineFunction(MachineFunction& MF) override {
      if (!clou::enabled.fps && !clou::enabled.prech && !clou::enabled.postch) {
	return false;
      }
      
      const X86Subtarget& STI = MF.getSubtarget<X86Subtarget>();
      const TargetInstrInfo *TII = STI.getInstrInfo();
      const bool Is64Bit = STI.is64Bit();
      const unsigned AddrBytes = Is64Bit ? 8 : 4;
      const Align AddrAlign(AddrBytes);
      const Function& F = MF.getFunction();
      const GlobalValue *sp;
      if (clou::enabled.fps) {
	sp = MF.getFunction().getParent()->getNamedValue((F.getName() + "_sp").str());
	assert(sp != nullptr);
      }
      const TargetRegisterInfo& TRI = *MF.getSubtarget().getRegisterInfo();

      for (auto& MBB : MF) {
	for (auto MBBI = MBB.begin(); MBBI != MBB.end(); ++MBBI) {
	  if (MBBI->isCall()) {
	    const auto& DL = MBBI->getDebugLoc();

	    if (clou::enabled.fps) {
	      // store SP right before
	      // if (!MF.getFunction().doesNotRecurse())
	      BuildMI(MBB, MBBI, DL, TII->get(Is64Bit ? X86::MOV64mr : X86::MOV32mr))
		.addReg(Is64Bit ? X86::RIP : X86::NoRegister)
		.addImm(1)
		.addReg(0)
		.addGlobalAddress(sp, 0)
		.addReg(0)
		.addReg(Is64Bit ? X86::RSP : X86::ESP)
		.addMemOperand(MBB.getParent()->getMachineMemOperand(MachinePointerInfo(sp),
								     MachineMemOperand::MOStore,
								     AddrBytes,
								     AddrAlign));
	      ++NumInstructions;
	    }

	    if (clou::enabled.prech && Is64Bit) {
	    
	      // Zero out any unused parameter-passing registers
	      for (const auto param_gpr : std::array<MCPhysReg, 6> {X86::EDI, X86::ESI, X86::EDX,
								    X86::ECX, X86::R8D, X86::R9D}) {
		if (!callUsesRegister(*MBBI, param_gpr, TRI)) {
		  // zero it out
		  BuildMI(MBB, MBBI, DL, TII->get(X86::XOR32rr), param_gpr)
		  .addReg(param_gpr)
		  .addReg(param_gpr);
		  ++NumInstructions;
		}
	      }

	      std::array<MCPhysReg, 8> param_fps = {X86::XMM0, X86::XMM1, X86::XMM2, X86::XMM3,
						    X86::XMM4, X86::XMM5, X86::XMM6, X86::XMM7};
	      if (any_of(param_fps, [&] (const MCPhysReg& reg) {
		return callUsesRegister(*MBBI, reg, TRI);
	      })) {
		for (const auto reg : param_fps) {
		  if (!callUsesRegister(*MBBI, reg, TRI)) {
		    BuildMI(MBB, MBBI, DL, TII->get(X86::XORPSrr), reg)
		      .addReg(reg)
		      .addReg(reg);
		    ++NumInstructions;
		  }
		}
	      } else {
#if 0
		BuildMI(MBB, MBBI, DL, TII->get(X86::VZEROALL));
#endif
		++NumInstructions;
	      }

	      ++NumMitigations;

	      if (clou::InsertTrapAfterMitigations)
		BuildMI(MBB, MBBI, DL, TII->get(X86::MFENCE));

	    }

	    if (clou::enabled.postch) {
	    
	      const auto MBBI_next = std::next(MBBI);
	      if (MBBI_next != MBB.end()) {
		// Check if SP is correct -- Spectre RSB hardening
		// xor rdi, rdi // rdi is dead, clobbered register
		// cmp rsp, [func_sp]
		// cmovne rsp, rdi
		// cmovne rbp, rdi
		// cmovne rbx, rdi
		const MCRegister ZeroReg = Is64Bit ? X86::RDI : X86::EDX;
		const MCRegister ZeroReg32 = Is64Bit ? X86::EDI : X86::EDX;
		BuildMI(MBB, MBBI_next, DL, TII->get(X86::XOR32rr), ZeroReg32)
		  .addReg(ZeroReg32)
		  .addReg(ZeroReg32);
		++NumInstructions;
		
		BuildMI(MBB, MBBI_next, DL, TII->get(Is64Bit ? X86::CMP64rm : X86::CMP32rm))
		  .addReg(Is64Bit ? X86::RSP : X86::ESP)
		  .addReg(Is64Bit ? X86::RIP : X86::NoRegister)
		  .addImm(1)
		  .addReg(0)
		  .addGlobalAddress(sp, 0)
		  .addReg(0)
		  .addMemOperand(MBB.getParent()->getMachineMemOperand(MachinePointerInfo(sp),
								       MachineMemOperand::MOStore,
								       AddrBytes,
								       AddrAlign));
		++NumInstructions;
		
		if (clou::enabled.fps) {
		  static const MCRegister CalleePreserved32[3] = {X86::ESP, X86::EBP, X86::EBX};
		  static const MCRegister CalleePreserved64[3] = {X86::RSP, X86::RBP, X86::RBX};
		  const MCRegister (*CalleePreserved)[3] = Is64Bit ? &CalleePreserved64 : &CalleePreserved32;
		  for (const auto reg : *CalleePreserved) {
		    BuildMI(MBB, MBBI_next, DL, TII->get(Is64Bit ? X86::CMOV64rr : X86::CMOV32rr), reg)
		      .addReg(reg)
		      .addReg(ZeroReg)
		      .addImm(X86::COND_NE);
		    ++NumInstructions;
		  }
		}

		++NumMitigations;

		if (clou::InsertTrapAfterMitigations)
		  BuildMI(MBB, MBBI_next, DL, TII->get(X86::MFENCE));
	      
	      }
	      
	    }
	  }
	}
      }

      if (clou::enabled.fps) {
	
	// well, save the base pointer at least
	const MCPhysReg csrs[] = {Is64Bit ? X86::RBX : X86::EBX};
	MF.getRegInfo().setCalleeSavedRegs(csrs);
	
      }

#if 0
      for (const MachineBasicBlock& MBB : MF) {
	for (const MachineInstr& MI : MBB) {
	  for (const MachineOperand& MO : MI.operands()) {
	    if (MO.isReg()) {
	      const Register Reg = MO.getReg();
	      if (Reg.isPhysical()) {
		const unsigned Bits = TRI.getRegSizeInBits(*TRI.getMinimalPhysRegClass(Reg.asMCReg()));
		if (Bits == 64) {
		  errs() << "64 BITS: " << MI;
		}
	      }
	    }
	  }
	}
      }
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
