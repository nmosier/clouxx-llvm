#pragma once

#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"

namespace llvm {

  extern cl::opt<bool> enable_no_spill_pass;
  
}
