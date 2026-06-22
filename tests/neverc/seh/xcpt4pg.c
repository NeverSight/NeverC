/*++

Copyright (c) Microsoft Corporation

Module Name:

    xcpt4pg.c

Abstract:

    This module implements Patch Guard user mode exception tests.

--*/

#include "stdio.h"
#include "setjmpex.h"
#include "float.h"
#include "windows.h"
#include "math.h"

DECLSPEC_NOINLINE
ULONG
PgFilter (
    VOID
    )

{

    printf("filter entered...");
    return EXCEPTION_EXECUTE_HANDLER;
}

VOID
PgTest69 (
    _Inout_ PLONG State,
    _Inout_ PLONG Fault
    )

{

    __try {
        __try {
            *Fault += 1;

        } __finally {
            if (AbnormalTermination()) {
                if (*State == 1) {
                    *State += 1;

                } else {
                    *Fault += 1;
                }
            }
        }

    } __except(((*State += 1) == 1) ? PgFilter() : EXCEPTION_CONTINUE_SEARCH) {
        if (*State != 2) {
            *Fault += 1;
        }
    }

    return;
}

#if defined(BAIL_IN_FINALLY)

VOID
PgTest70 (
    _Inout_ PLONG State,
    _Inout_ PLONG Fault
    )

{

    __try {
        __try {
            *Fault += 1;

        } __finally {
            if (AbnormalTermination()) {
                if (*State == 2) {
                    PgFilter();
                    return;

                } else {
                    *Fault += 1;
                }
            }
        }

    } __except(((*State += 2) == 2) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        *Fault += 1;
    }

    return;
}

VOID
PgTest71 (
    _Inout_ PLONG State,
    _Inout_ PLONG Fault
    )

{

    __try {
        __try {
           __try {
               *Fault += 1;

           } __finally {
               if (AbnormalTermination()) {
                   if (*State == 3) {
                       *State += 3;
                       return;
   
                   } else {
                       *Fault += 1;
                   }
               }
           }

        } __finally {
            if (AbnormalTermination()) {
                if (*State == 6) {
                    *State += 3;

                } else {
                    *Fault += 1;
                }
            }
        }

    } __except(((*State += 3) == 3) ? PgFilter() : EXCEPTION_CONTINUE_SEARCH) {
        *Fault += 1;
    }

    return;
}

VOID
PgTest72 (
    _Inout_ PLONG State,
    _Inout_ PLONG Fault
    )

{

    __try {
        __try {
           __try {
               *Fault += 1;

           } __finally {
               if (AbnormalTermination()) {
                   if (*State == 4) {
                       *State += 4;
                       return;
   
                   } else {
                       *Fault += 1;
                   }
               }
           }

        } __finally {
            if (AbnormalTermination()) {
                if (*State == 8) {
                    *State += 4;
                    PgFilter();

                } else {
                    *Fault += 1;
                }
            }
        }

    } __except(((*State += 4) == 4) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        *Fault += 1;
    }

    return;
}

VOID
PgTest73 (
    _Inout_ PLONG State,
    _Inout_ PLONG Fault
    )

{

    __try {
        __try {
           __try {
               *Fault += 1;

           } __finally {
               if (AbnormalTermination()) {
                   if (*State == 5) {
                       *State += 5;
   
                   } else {
                       *Fault += 1;
                   }
               }
           }

        } __finally {
            if (AbnormalTermination()) {
                if (*State == 10) {
                    *State += 5;
                    return;

                } else {
                    *Fault += 1;
                }
            }
        }

    } __except(((*State += 5) == 5) ? PgFilter() : EXCEPTION_CONTINUE_SEARCH) {
        *Fault += 1;
    }

    return;
}

VOID
PgTest74 (
    _Inout_ PLONG State,
    _Inout_ PLONG Fault
    )

{

    __try {
        __try {
           __try {
               *Fault += 1;

           } __finally {
               if (AbnormalTermination()) {
                   if (*State == 6) {
                       *State += 6;
   
                   } else {
                       *Fault += 1;
                   }
               }
           }

        } __finally {
            if (AbnormalTermination()) {
                if (*State == 12) {
                    *State += 6;
                    PgFilter();
                    return;

                } else {
                    *Fault += 1;
                }
            }
        }

    } __except(((*State += 6) == 6) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        *Fault += 1;
    }

    return;
}

VOID
PgTest75 (
    _Inout_ PLONG State,
    _Inout_ PLONG Fault
    )

{

    __try {
        __try {
           __try {
               __try {
                   *Fault += 1;
    
               } __finally {
                   if (AbnormalTermination()) {
                       if (*State == 7) {
                           *State += 7;
                           *Fault += 1;
       
                       } else {
                           *State += 10;
                       }
                   }
               }

            } __except(((*State += 7) == 7) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
                *Fault += 1;
            }
    
        } __finally {
            if (AbnormalTermination()) {
                if (*State == 28) {
                    *State += 7;
                    return;

                } else {
                    *Fault += 1;
                }
            }
        }

    } __except(((*State += 7) == 28) ? PgFilter() : EXCEPTION_CONTINUE_SEARCH) {
        *Fault += 1;
    }

    return;
}

VOID
PgTest76 (
    _Inout_ PLONG State,
    _Inout_ PLONG Fault
    )

{

    __try {
        __try {
           __try {
               __try {
                   *Fault += 1;
    
               } __finally {
                   if (AbnormalTermination()) {
                       if (*State == 8) {
                           *State += 8;
                           *Fault += 1;
       
                       } else {
                           *State += 10;
                       }
                   }
               }

            } __except(((*State += 8) == 8) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
                *Fault += 1;
            }
    
        } __finally {
            if (AbnormalTermination()) {
                if (*State == 32) {
                    *State += 8;
                    PgFilter();
                    return;

                } else {
                    *Fault += 1;
                }
            }
        }

    } __except(((*State += 8) == 32) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        *Fault += 1;
    }

    return;
}

VOID
PgTest77 (
    _Inout_ PLONG State,
    _Inout_ PLONG Fault
    )

{

    __try {
        __try {
           __try {
               __try {
                   *Fault += 1;
    
               } __finally {
                   if (AbnormalTermination()) {
                       if (*State == 9) {
                           *State += 9;
                           *Fault += 1;
       
                       } else {
                           *State += 10;
                       }
                   }
               }

            } __except(((*State += 9) == 9) ? PgFilter() : EXCEPTION_CONTINUE_SEARCH) {
                *Fault += 1;
            }
    
        } __finally {
            if (AbnormalTermination()) {
                if (*State == 36) {
                    *State += 9;
                    return;

                } else {
                    *Fault += 1;
                }
            }
        }

    } __except(((*State += 9) == 36) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        *Fault += 1;
    }

    return;
}

VOID
PgTest78 (
    _Inout_ PLONG State,
    _Inout_ PLONG Fault
    )

{

    __try {
        __try {
           __try {
               __try {
                   *Fault += 1;
    
               } __finally {
                   if (AbnormalTermination()) {
                       if (*State == 10) {
                           *State += 10;
                           PgFilter();
                           *Fault += 1;
       
                       } else {
                           *State += 10;
                       }
                   }
               }

            } __except(((*State += 10) == 10) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
                *Fault += 1;
            }
    
        } __finally {
            if (AbnormalTermination()) {
                if (*State == 40) {
                    *State += 10;
                    return;

                } else {
                    *Fault += 1;
                }
            }
        }

    } __except(((*State += 10) == 40) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        *Fault += 1;
    }

    return;
}

#endif

VOID
PgTests (
    VOID
    )
{

    PLONG BadAddress;
    LONG Counter;

    //
    // Initialize pointers.
    //

    BadAddress = (PLONG)NULL;

    //
    // Patch guard test 69.
    //

    printf("    test69...");
    Counter = 0;
    __try {
        PgTest69(&Counter, BadAddress);

    } __except(EXCEPTION_EXECUTE_HANDLER) {
        printf("unexpected exception...");
    }

    if (Counter != 2) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    printf("    test70...");
#if !defined(BAIL_IN_FINALLY)
    printf("skipped\n");
#else
    Counter = 0;
    __try {
        PgTest70(&Counter, BadAddress);

    } __except(EXCEPTION_EXECUTE_HANDLER) {
        printf("unexpected exception...");
    }

    if (Counter != 2) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }
#endif

    printf("    test71...");
#if !defined(BAIL_IN_FINALLY)
    printf("skipped\n");
#else
    Counter = 0;
    __try {
        PgTest71(&Counter, BadAddress);

    } __except(EXCEPTION_EXECUTE_HANDLER) {
        printf("unexpected exception...");
    }

    if (Counter != 9) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }
#endif

    printf("    test72...");
#if !defined(BAIL_IN_FINALLY)
    printf("skipped\n");
#else
    Counter = 0;
    __try {
        PgTest72(&Counter, BadAddress);

    } __except(EXCEPTION_EXECUTE_HANDLER) {
        printf("unexpected exception...");
    }

    if (Counter != 12) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }
#endif

    printf("    test73...");
#if !defined(BAIL_IN_FINALLY)
    printf("skipped\n");
#else
    Counter = 0;
    __try {
        PgTest73(&Counter, BadAddress);

    } __except(EXCEPTION_EXECUTE_HANDLER) {
        printf("unexpected exception...");
    }

    if (Counter != 15) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }
#endif

    printf("    test74...");
#if !defined(BAIL_IN_FINALLY)
    printf("skipped\n");
#else
    Counter = 0;
    __try {
        PgTest74(&Counter, BadAddress);

    } __except(EXCEPTION_EXECUTE_HANDLER) {
        printf("unexpected exception...");
    }

    if (Counter != 18) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }
#endif

    printf("    test75...");
#if !defined(BAIL_IN_FINALLY)
    printf("skipped\n");
#else
    Counter = 0;
    __try {
        PgTest75(&Counter, BadAddress);

    } __except(EXCEPTION_EXECUTE_HANDLER) {
        printf("unexpected exception...");
    }

    if (Counter != 35) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }
#endif

    printf("    test76...");
#if !defined(BAIL_IN_FINALLY)
    printf("skipped\n");
#else
    Counter = 0;
    __try {
        PgTest76(&Counter, BadAddress);

    } __except(EXCEPTION_EXECUTE_HANDLER) {
        printf("unexpected exception...");
    }

    if (Counter != 40) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }
#endif

    printf("    test77...");
#if !defined(BAIL_IN_FINALLY)
    printf("skipped\n");
#else
    Counter = 0;
    __try {
        PgTest77(&Counter, BadAddress);

    } __except(EXCEPTION_EXECUTE_HANDLER) {
        printf("unexpected exception...");
    }

    if (Counter != 45) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }
#endif

    printf("    test78...");
#if !defined(BAIL_IN_FINALLY)
    printf("skipped\n");
#else
    Counter = 0;
    __try {
        PgTest78(&Counter, BadAddress);

    } __except(EXCEPTION_EXECUTE_HANDLER) {
        printf("unexpected exception...");
    }

    if (Counter != 50) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }
#endif

}

