#ifndef LLVM_CLOU_CLOU_H
#define LLVM_CLOU_CLOU_H

#include <string>

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/StringRef.h"

namespace clou {
  
  extern bool ClouNoSpill;
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
