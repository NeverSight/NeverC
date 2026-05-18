#ifndef __CET_H
#define __CET_H

#ifdef __ASSEMBLER__

#ifndef __CET__
#define _CET_ENDBR
#endif

#ifdef __CET__

#if __CET__ & 0x1
#define _CET_ENDBR endbr64
#else
#define _CET_ENDBR
#endif

#define __PROPERTY_ALIGN 3

.pushsection ".note.gnu.property", "a".p2align __PROPERTY_ALIGN.long 1f -
                                       0f /* name length.  */
                                           .long 4f -
                                       1f /* data length.  */
                                           /* NT_GNU_PROPERTY_TYPE_0.   */
                                           .long 5     /* note type.  */
                                       0 :.asciz "GNU" /* vendor name.  */
                                          1
    :.p2align __PROPERTY_ALIGN
                                           /* GNU_PROPERTY_X86_FEATURE_1_AND. */
                                           .long 0xc0000002 /* pr_type.  */
                                           .long 3f -
                                       2f /* pr_datasz.  */
                                       2 :
                                           /* GNU_PROPERTY_X86_FEATURE_1_XXX. */
                                           .long __CET__ 3
    :.p2align __PROPERTY_ALIGN 4 :.popsection
#endif
#endif
#endif
