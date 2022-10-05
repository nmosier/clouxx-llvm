#ifndef LLVM_CLOU_CLOU_H
#define LLVM_CLOU_CLOU_H

namespace clou {

  enum class StackMitigationMode {
    FunctionPrivateStacks,
    Lfence,
  };
  extern StackMitigationMode stack_mitigation_mode;

  extern bool ClouNoSpill;
  extern bool ClouSpectreRSB;
}

#endif
