#pragma once

// CJumpAction: both retail regions use these same addresses. Each version's
// body was checked independently; its actor lookup calls differ by region.
#ifdef HULK_USA
#define HULK_JUMP_START sub_00013B90
#define HULK_JUMP_UPDATE sub_00018D40
#else
#define HULK_JUMP_START sub_00013B90
#define HULK_JUMP_UPDATE sub_00018D40
#endif
