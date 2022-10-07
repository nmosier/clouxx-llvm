#ifndef LLVM_CLOU_CLOU_H
#define LLVM_CLOU_CLOU_H

#include <string>

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/StringRef.h"

namespace clou {

  enum class StackMitigationMode {
    FunctionPrivateStacks,
    Lfence,
  };
  extern StackMitigationMode stack_mitigation_mode;

  extern bool ClouNoSpill;
  extern bool ClouSpectreRSB;
  extern bool ClouLog;
  extern std::string ClouLogDir;

  struct Subcomponents {
    bool udt;
    bool oobs;
    bool fps;
    bool prech;
    bool postch;
    Subcomponents();
    Subcomponents(llvm::StringRef s);
  };

  extern Subcomponents enabled;
  
}

#endif
