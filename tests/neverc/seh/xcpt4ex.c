
#include "stdio.h"
#include "setjmpex.h"
#include "float.h"
#include "windows.h"
#include "math.h"

//
// Test82 and Test90 exercise C++ throw/catch interacting with SEH
// __try/__finally; their bodies live in xcpt4cxx.cpp. neverc is a C-only
// compiler, so this C-only extraction of the XCPT4 suite skips them. Define
// XCPT4_HAVE_CXX_EH (and link an xcpt4cxx.obj built by a C++ compiler) to
// run them.
//
#if defined(XCPT4_HAVE_CXX_EH)
VOID
Test82 (
    _Inout_ PLONG Counter
    );

VOID
Test90 (
    _Inout_ PLONG State
    );
#endif

VOID
Test79 (
    _Inout_ PLONG Counter,
    _Inout_ PLONG Fault
    )

{

    __try {
        __try {
            __try {
                *Fault += 1;

            } __finally {
                printf("finally 1...");
                *Fault += 1;
            }

        } __finally {
            printf("finally 2...");
        }

#pragma prefast(suppress: __WARNING_EXCEPTIONCONTINUESEARCH, "Exception test.")
   } __except(*Counter += 1, printf("filter 1..."), EXCEPTION_CONTINUE_SEARCH) { 
        ;
   }

   return;
}

ULONG G;

ULONG
Test80 (
    VOID
    )

{

    G = 1;
    __try {
        while (G) {
            __try {
                if (G == 10) {
                    return 1;
                }

                if (G == 1) {
                    continue;
                }

            } __finally {
                G = 0;
            }
        }

    } __finally {
        G = 10;
    }

    return 0;
}

VOID
Test81 (
    _Inout_ PLONG Counter
    )

{

    volatile PUCHAR AvPtr = NULL;

    __try {
        __try {
            *AvPtr = '\0';
    
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            __leave;
        }

    } __finally {
        *Counter += 1;
    }

    return;
}

LONG
Test83 (
    VOID
    )

{
    
    G = 1;
    __try {
        __try {
            while (G) {
                __try {
                    if (G == 10) {
                        return 1;
                    }

                    if (G == 1) {
                        continue;
                    }

                } __finally {
                    G = 0;
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            __leave;
        }

    } __finally {
        G = 10;
    }

    return 0;
}

DECLSPEC_NOINLINE
VOID
Test84 (
    _Inout_ PLONG Counter
    )

{   
    volatile int *Fault = 0;

    __try {
        __try {
            *Fault += 1;

        } __except(EXCEPTION_EXECUTE_HANDLER) {
            __try {
                return;

            } __finally {
                *Counter += 1;
            }
        }
    } __finally {

        if (AbnormalTermination()) {
            *Counter += 1;
        }
    }

    return;
}

DECLSPEC_NOINLINE
LONG
Test85 (
    _Inout_ PLONG Counter
    )

{   
    volatile int *Fault = 0;

    G = 1;
    __try {
        __try {
            __try {
                while (G) {
                    __try {
                        __try {
                            if (G == 10) {
                                return 1;
                            }
                            __try {
                                *Counter += 1;
                            } __except(EXCEPTION_EXECUTE_HANDLER) {
                            }

                            if (G == 1) {
                                continue;
                            }

                        } __finally {
                            G = 0;
                            *Counter += 1;
                            *Fault += 1;
                        }
                    } __except(EXCEPTION_EXECUTE_HANDLER) {
                        *Counter += 1;
                        __leave;
                    }
                }

            } __finally {
                G = 10;
                *Counter += 1;
                *Fault += 1;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            *Counter += 1;
        }
        *Counter += 1;
    } __finally {
        *Counter += 1;
    }
    return 1;
}

DECLSPEC_NOINLINE
VOID
Test86 (
    _Inout_ PLONG Counter
    )

{   
    volatile int *Fault = 0;

    __try {
        __try {
            __try {
                __try {
                    __try {
                        __try {
                            *Fault += 1;

                        } __except(EXCEPTION_EXECUTE_HANDLER) {
                            __try {
                                return;

                            } __finally {
                                *Counter += 1;
                            }
                        }
                    } __finally {
                        *Counter += 1;
                    }
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    __leave;
                }
            } __finally {
                *Counter += 1;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            __leave;
        }
    } __finally {
        *Counter += 1;
    }

    return;
}

#if defined(BAIL_IN_FINALLY)

#pragma warning(push)
#pragma warning(disable:4532)

VOID
Test87(
    _Inout_ PLONG Counter
    )

/*++

Routine Description:

    This function verifies the behavior of nested exception dispatching.

Arguments:

    Counter - Supplies a pointer to the state counter.

Return Value:
    None.

--*/

{
    volatile int *Fault = 0;

    //
    // N.B.  Disabled on x86 due to failing test case with handling of returns
    //       in nested termination handlers on x86.
    //

    __try {
        __try {
            __try {
                __try {
                    __try {
                        *Fault += 1;

                        __try {

                        } __finally {
                            if (AbnormalTermination()) {
                                *Fault += 1;
                            }
                        }
                    } __finally {

                        if (AbnormalTermination()) {
                            if ((*Counter += 13) == 26) {
                                return;

                            } else {
                                *Fault += 1;
                            }
                        }
                    }
                } __finally {
                    if (AbnormalTermination()) {
                        *Counter += 13;
                        *Fault += 1;

                    }
                }

            } __except(((*Counter += 13) == 13) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
                *Fault += 1;

            }
        } __except(((*Counter += 13) == 65) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            __try {
                *Counter += 13;
                return;

            } __finally {
                if (AbnormalTermination()) {
                    *Counter += 13;
                    goto Finish;

                }

            }
        }
    } __finally {

        if (AbnormalTermination()) {
            if ((*Counter += 13) == 104) {
                goto Finish;
            }
        }
    }

Finish:

    return;
}

#pragma warning(pop)

VOID
Test88 (
    _Inout_ PLONG Counter
    )

{
    volatile int *Fault = 0;

    __try {
        __try {
            __try {
                __try {
                    __try {
                        __try {
                            __try {
                                __try {
                                    *Fault += 1;
                                } __except(((*Counter += 1) == 1) ? *Fault : EXCEPTION_CONTINUE_SEARCH) {
                                }
                            } __except(*Counter += 1, EXCEPTION_EXECUTE_HANDLER) {
                                *Fault += 2;
                            }
                        } __except(*Counter += 1, EXCEPTION_CONTINUE_SEARCH) {
                            __leave;
                        }
                    } __except(*Counter += 1, EXCEPTION_CONTINUE_SEARCH) {
                        __leave;
                    }
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            __leave;
        }
    } __finally {
        *Counter += 1;
    }
}

VOID
Test89 (
    _Inout_ PLONG Counter
    )

{
    volatile int *Fault = 0;

    __try {
        __try {
            __try {
                *Fault += 1;

                __try {
                    *Fault += 1;

                } __finally {
                    if (AbnormalTermination()) {
                        *Counter += 14;
                    }
                }
            } __finally {
                if (AbnormalTermination()) {
                    *Counter += 14;
                    return;

                }
            }
        } __finally {
            if (AbnormalTermination()) {
                *Counter += 14;
                *Fault += 1;

            }
        }
    } __except(((*Counter += 14) == 56) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_EXECUTE_HANDLER) {
        *Counter += 13;
    }
}

#endif

VOID
ExTests (
    VOID
    )
{
    PLONG BadAddress;
    LONG Counter;

    //
    // Initialize pointers.
    //

    BadAddress = (PLONG)NULL;

    printf("    test79...");
    Counter = 0;
    __try {
        Test79(&Counter, BadAddress);

    } __except(printf("filter 2..."), EXCEPTION_EXECUTE_HANDLER) {
        Counter += 1;
    }

    if (Counter == 3) {
        printf("passed\n");

    } else {
        printf("failed\n");
    }

    printf("    test80...");
    if (Test80() != 0) {
        printf("failed\n");

    } else {
        printf("passed\n");
    }

    printf("    test81...");
    Counter = 0;
    Test81(&Counter);
    if (Counter != 1) {
        printf("failed\n");

    } else {
        printf("passed\n");
    }

    printf("    test82...");
#if !defined(XCPT4_HAVE_CXX_EH)
    printf("skipped\n");
#else
    Counter = 1;
    Test82(&Counter);
    if (Counter != 0) {
        printf("failed\n");

    } else {
        printf("passed\n");
    }
#endif

    printf("    test83...");
    if (Test83() != 0) {
        printf("failed\n");

    } else {
        printf("passed\n");
    }

    printf("    test84...");
    Counter = 0;
    Test84(&Counter);
    if (Counter != 2) {
        printf("failed\n");

    } else {
        printf("passed\n");
    }

    printf("    test85...");
    Counter = 0;
    Test85(&Counter);
    if (Counter != 7) {
        printf("failed\n");

    } else {
        printf("passed\n");
    }

    printf("    test86...");
    Counter = 0;
    Test86(&Counter);
    if (Counter != 4) {
        printf("failed %d\n", Counter);

    } else {
        printf("passed\n");
    }

    printf("    test87...");
#if defined(_M_IX86) || !defined(BAIL_IN_FINALLY)
    printf("skipped\n");
#else
    Counter = 0;
    Test87(&Counter);
    if (Counter != 104) {
        printf("failed %d\n", Counter);

    } else {
        printf("passed\n");
    }
#endif

    printf("    test88...");
#if !defined(BAIL_IN_FINALLY)
    printf("skipped\n");
#else
    Counter = 0;
    Test88(&Counter);
    if (Counter != 6) {
        printf("failed %d\n", Counter);

    } else {
        printf("passed\n");
    }
#endif

    printf("    test89...");
#if defined(_M_IX86) || !defined(BAIL_IN_FINALLY)
    printf("skipped\n");
#else
    Counter = 0;
    Test89(&Counter);
    if (Counter != 69) {
        printf("failed %d\n", Counter);

    } else {
        printf("passed\n");
    }
#endif

    printf("    test90...");
#if !defined(XCPT4_HAVE_CXX_EH)
    printf("skipped\n");
#else
    Counter = 0;
    Test90(&Counter);
    if (Counter != 0) {
        printf("failed %d\n", Counter);

    } else {
        printf("passed\n");
    }
#endif
}

