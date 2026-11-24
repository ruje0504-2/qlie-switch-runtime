// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifdef __SWITCH__
#include "diagnostics.h"
#include <switch.h>
#include <cstdio>

namespace whj2 {
struct ThreadBinding {
 u64 allowed=0, actual=0;
 s32 requested=-1, preferred=-1;
 Result setResult=0, readResult=0;
};

// Ordinals refer to the process's allowed cores. On restricted launches the
// last available core can serve more than one role.
inline ThreadBinding bindSwitchThread(unsigned ordinal){
 ThreadBinding binding;
 binding.setResult=svcGetInfo(&binding.allowed,InfoType_CoreMask,CUR_PROCESS_HANDLE,0);
 if(R_SUCCEEDED(binding.setResult)){
  for(unsigned core=0;core<32;++core){
   if(!(binding.allowed&(1ull<<core)))continue;
   binding.requested=s32(core);
   if(ordinal==0)break;
   --ordinal;
  }
  if(binding.requested>=0){
   binding.setResult=svcSetThreadCoreMask(threadGetCurHandle(),binding.requested,1u<<binding.requested);
  }
 }
 if constexpr (qlie::diagnostics) binding.readResult=svcGetThreadCoreMask(&binding.preferred,&binding.actual,threadGetCurHandle());
 return binding;
}

inline void logSwitchThread(const char*name,const ThreadBinding&binding){
 bool fixed=binding.requested>=0&&R_SUCCEEDED(binding.setResult)&&R_SUCCEEDED(binding.readResult)
            &&binding.actual==(1ull<<binding.requested)&&binding.preferred==binding.requested;
 QLIE_LOGF("CPU role=%s allowed=%llx requested=%d preferred=%d actual_mask=%llx fixed=%d set_rc=%x read_rc=%x\n",
              name,(unsigned long long)binding.allowed,int(binding.requested),int(binding.preferred),
              (unsigned long long)binding.actual,int(fixed),unsigned(binding.setResult),unsigned(binding.readResult));
}
}
#endif
