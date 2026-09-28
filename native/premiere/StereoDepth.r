#include "AEConfig.h"
#include "AE_EffectVers.h"
#ifndef AE_OS_WIN
#include "AE_General.r"
#endif

resource 'PiPL' (16000) {
  {
    Kind { AEEffect },
    Name { "2D to 3D" },
    Category { "StereoDepth" },
#ifdef AE_OS_WIN
    CodeWin64X86 { "EffectMain" },
#else
    CodeMacARM64 { "EffectMain" },
#endif
    AE_PiPL_Version { 2, 0 },
    AE_Effect_Spec_Version { PF_PLUG_IN_VERSION, PF_PLUG_IN_SUBVERS },
    AE_Effect_Version { 131073 },
    AE_Effect_Info_Flags { 0 },
    AE_Effect_Global_OutFlags { 0 },
    AE_Effect_Global_OutFlags_2 { 8 },
    AE_Effect_Match_Name { "StereoDepth.2Dto3D" },
    AE_Reserved_Info { 0 }
  }
};
