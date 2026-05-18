//===-- X86DarwinSdkConflictUndef.h - Undef SDK macros that clash -*- C++
//-*-===//
//
// Darwin's <sys/param.h> (often pulled in via <sys/mount.h> from Support/Path)
// defines FSCALE, which collides with x86 instruction enumerator names in
// TableGen outputs.
//
// Intentionally has no include guard: this header may be included multiple
// times in one TU so FSCALE stays cleared before each generated fragment.
//
//===----------------------------------------------------------------------===//

#ifdef FSCALE
#undef FSCALE
#endif
