#ifndef LLVM_CLOU_CLOU_H
#define LLVM_CLOU_CLOU_H

#include <string>

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Function.h"
#include "llvm/CodeGen/MachineFunction.h"

namespace clou {
  
  extern bool ClouNoSpill;
  extern bool ClouLog;
  extern std::string ClouLogDir;

#define SUBCOMPONENTS_X(X, Y)			\
  X(ncal_xmit, true)				\
  X(ncal_glob, true)				\
  X(ncas_xmit, true)				\
  X(ncas_ctrl, true)				\
  X(entry_xmit, false)				\
  X(load_xmit, false)				\
  X(call_xmit, false)				\
  X(fps, true)					\
  X(ncsrs, true)				\
  X(stackinit, false)				\
  X(prech, true)				\
  Y(postch, true)
  

  
  struct Subcomponents {
#define SUBCOMPONENTS_DEF_Y(name, init) bool name
#define SUBCOMPONENTS_DEF_X(name, init) SUBCOMPONENTS_DEF_Y(name, init);
    SUBCOMPONENTS_X(SUBCOMPONENTS_DEF_X, SUBCOMPONENTS_DEF_Y);
#undef SUBCOMPONENTS_DEF_X
#undef SUBCOMPONENTS_DEF_Y
    Subcomponents();
    Subcomponents(llvm::StringRef s);
  };

  extern Subcomponents enabled;

  extern bool whitelisted(const llvm::Function& F);

  extern size_t max_inline_instructions;
  extern size_t max_inline_count;

  extern bool UnsafeAA;
  extern bool WeightGraph;

  extern bool InsertTrapAfterMitigations;

  extern bool PSF;

  enum class MinCutAlgKind {
    SMT,
    GREEDY,
  };

  extern MinCutAlgKind MinCutAlg;

  extern bool RestrictedPSF;

  extern float LoopWeight;
  extern float DominatorWeight;
  extern float STWeight;

  extern bool ExpandSTs;

  extern llvm::StringRef FnAttr_fps_usestack;
  extern const uint64_t StackSize;

  extern bool StrictCallingConv;

  extern bool NCASAll;
  extern float Timeout;
}


#endif
