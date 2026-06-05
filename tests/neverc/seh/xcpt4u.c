/*++

Copyright (c) Microsoft Corporation

Module Name:

    xcpt4u.c

Abstract:

    This module implements user mode exception tests.

--*/

#include "stdio.h"
#include "setjmpex.h"
#include "float.h"
#include "windows.h"
#include "math.h"

#if !defined(STATUS_SUCCESS)
# define STATUS_SUCCESS         (0)
#endif

#if !defined(STATUS_UNSUCCESSFUL)
# define STATUS_UNSUCCESSFUL    (0xC0000001UL)
#endif

#if !defined(EXCEPTION_NESTED_CALL)
#define EXCEPTION_NESTED_CALL   (0x10)
#endif

#define RaiseStatus(x) RaiseException((x), 0, 0, NULL)

#if defined(_M_AMD64)
#define NEST_IN_FINALLY /* allow when __try nested in __finally OK */
#endif

#if defined(_M_ARM) || defined(_M_ARM64)
#define DO_ALIGNMENT_TEST
#endif

#if defined(_M_IX86) || defined(_M_AMD64)
#define DO_FLOAT_EXCEPTION_TEST
#endif

//
// Define switch constants.
//

#define BLUE 0
#define RED 1


VOID
PgTests (
    VOID
    );

VOID
ExTests (
    VOID
    );

VOID
addtwo (
    _In_ LONG First,
    _In_ LONG Second,
    _Out_ PLONG Place
)

{

    RaiseStatus(STATUS_INTEGER_OVERFLOW);
    *Place = First + Second;
    return;
}

VOID
foo1(
    _In_ DWORD Status
)

{

    //
    // Raise exception.
    //

    RaiseStatus(Status);
    return;
}

VOID
foo2(
    _Inout_ PLONG BlackHole,
    _Inout_ PLONG BadAddress,
    _In_ BOOLEAN Interlock
)

{

    //
    // Raise exception.
    //

    if (Interlock != FALSE) {
        *BlackHole += InterlockedAdd(BadAddress, 0);
    }
    else {
        *BlackHole += *BadAddress;
    }
    return;
}

VOID
bar1 (
    _In_ DWORD Status,
    _Out_ PLONG Counter
    )
{

    __try {
        foo1(Status);

    } __finally {
        if (abnormal_termination() != FALSE) {
            *Counter = 99;

        } else {
            *Counter = 100;
        }
    }

    return;
}

VOID
bar2 (
    _In_ PLONG BlackHole,
    _In_ PLONG BadAddress,
    _Out_ PLONG Counter
    )
{

    __try {
        foo2(BlackHole, BadAddress, FALSE);

    } __finally {
        if (abnormal_termination() != FALSE) {
            *Counter = 99;

        } else {
            *Counter = 100;
        }
    }

    return;
}

VOID
dojump (
    _In_ jmp_buf JumpBuffer,
    _Inout_ PLONG Counter
    )

{

    __try {
        __try {
            *Counter += 1;
            RaiseStatus(STATUS_INTEGER_OVERFLOW);

        } __finally {
            *Counter += 1;
        }

    } __finally {
        *Counter += 1;
        longjmp(JumpBuffer, 1);
    }
}

VOID
eret (
    _In_ DWORD Status,
    _Inout_ PLONG Counter
    )

{

    __try {
        __try {
            foo1(Status);

        } __except((GetExceptionCode() == Status) ?
                 EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            *Counter += 1;
            return;
        }

    } __finally {
        *Counter += 1;
    }

    return;
}

ULONG
except2(
    _In_ PEXCEPTION_POINTERS ExceptionPointers,
    _Inout_ PLONG Counter
)

{

    PEXCEPTION_RECORD ExceptionRecord;

    ExceptionRecord = ExceptionPointers->ExceptionRecord;
    if ((ExceptionRecord->ExceptionCode == STATUS_UNSUCCESSFUL) &&
        ((ExceptionRecord->ExceptionFlags & EXCEPTION_NESTED_CALL) == 0)) {
        *Counter += 11;
        return EXCEPTION_EXECUTE_HANDLER;

    }
    else {
        *Counter += 13;
        return EXCEPTION_CONTINUE_SEARCH;
    }
}

ULONG
except3(
    _In_ PEXCEPTION_POINTERS ExceptionPointers,
    _Inout_ PLONG Counter
)

{

    PEXCEPTION_RECORD ExceptionRecord;

    ExceptionRecord = ExceptionPointers->ExceptionRecord;
    if ((ExceptionRecord->ExceptionCode == STATUS_INTEGER_OVERFLOW) &&
        ((ExceptionRecord->ExceptionFlags & EXCEPTION_NESTED_CALL) == 0)) {
        *Counter += 17;
        RaiseStatus(STATUS_UNSUCCESSFUL);

    }
    else if ((ExceptionRecord->ExceptionCode == STATUS_UNSUCCESSFUL) &&
        ((ExceptionRecord->ExceptionFlags & EXCEPTION_NESTED_CALL) != 0)) {
        *Counter += 19;
        return EXCEPTION_CONTINUE_SEARCH;
    }

    *Counter += 23;
    return EXCEPTION_EXECUTE_HANDLER;
}

VOID
except1 (
    _Inout_ PLONG Counter
    )

{

    __try {
        *Counter += 5;
        RaiseStatus(STATUS_INTEGER_OVERFLOW);

    } __except(except3(GetExceptionInformation(), Counter)) {
        *Counter += 7;
    }

    *Counter += 9;
    return;
}

#if defined(BAIL_IN_FINALLY)
VOID
fret (
    _Inout_ PLONG Counter
    )

{

    __try {
        __try {
            *Counter += 1;

        } __finally {
            *Counter += 1;
            return;
        }
    } __finally {
        *Counter += 1;
    }

    return;
}
#endif

LONG
Echo (
    _In_ LONG Value
    )

{
    return Value;
}

double
SquareDouble (
    _In_ double op
    )
{
    /* Note - was originally "return op * op;", but under autoinlining
     * with a constant argument, the calculation will be done at compile
     * time.  This way prevents constant folding, so we really get a
     * run-time overflow exception. */
    return exp(2.0 * log(op));
}

#if defined(NEST_IN_FINALLY)
VOID
Test61Part2 (
    _Inout_ PLONG Counter
    )
{
    __try {
        *Counter -= 1;
        RaiseStatus(STATUS_INTEGER_OVERFLOW);
    } __finally {
        __try {
            *Counter += 2;
            RaiseStatus(STATUS_INTEGER_OVERFLOW);

        } __except(EXCEPTION_EXECUTE_HANDLER) {
            *Counter += 5;
        }
        *Counter += 7;
    }
}
#endif  /* def(NEST_IN_FINALLY) */

#pragma warning(push)

#if defined(BAIL_IN_FINALLY)
#pragma warning(disable:4532)
#endif


int
__cdecl
main (
    __in int argc,
    __in_ecount(argc) char *argv[]
    )
{

    PLONG BadAddress;
    PCHAR BadByte;
    PLONG BlackHole;
    ULONG Index1;
    ULONG Index2 = RED;
    jmp_buf JumpBuffer;
    LONG Counter;

    //
    // Announce start of exception test.
    //

    printf("Start of exception test\n");

    //
    // Initialize pointers.
    //

    BadAddress = (PLONG)NULL;
    BadByte = (PCHAR)NULL;
    BadByte += 4095;
    BlackHole = &Counter;

    //
    // Simply try statement with a finally clause that is entered sequentially.
    //

    printf("    test1...");
    Counter = 0;
    __try {
        Counter += 1;

    } __finally {
        if (abnormal_termination() == FALSE) {
            Counter += 1;
        }
    }

    if (Counter != 2) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }
    
    //
    // Simple try statement with an exception clause that is never executed
    // because there is no exception raised in the try clause.
    //

    printf("    test2...");
    Counter = 0;
    __try {
        Counter += 1;

    } __except(Counter) {
        Counter += 1;
    }

    if (Counter != 1) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Simple try statement with an exception handler that is never executed
    // because the exception expression continues execution.
    //

    printf("    test3...");
    Counter = 0;
    __try {
        Counter -= 1;
        RaiseStatus(STATUS_INTEGER_OVERFLOW);

    } __except(Counter) {
        Counter -= 1;
    }

    if (Counter != - 1) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Simple try statement with an exception clause that is always executed.
    //

    printf("    test4...");
    Counter = 0;
    __try {
        Counter += 1;
        RaiseStatus(STATUS_INTEGER_OVERFLOW);

    } __except(Counter) {
        Counter += 1;
    }

    if (Counter != 2) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Simple try statement with an exception clause that is always executed.
    //

    printf("    test5...");
    Counter = 0;
    __try {
        Counter += 1;
        *BlackHole += *BadAddress;

    } __except(Counter) {
        Counter += 1;
    }

    if (Counter != 2) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Simply try statement with a finally clause that is entered as the
    // result of an exception.
    //

    printf("    test6...");
    Counter = 0;
    __try {
        __try {
            Counter += 1;
            RaiseStatus(STATUS_INTEGER_OVERFLOW);

        } __finally {
            if (abnormal_termination() != FALSE) {
                Counter += 1;
            }
        }

    } __except(Counter) {
        if (Counter == 2) {
            Counter += 1;
        }
    }

    if (Counter != 3) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Simply try statement with a finally clause that is entered as the
    // result of an exception.
    //

    printf("    test7...");
    Counter = 0;
    __try {
        __try {
            Counter += 1;
            *BlackHole += *BadAddress;

        } __finally {
            if (abnormal_termination() != FALSE) {
                Counter += 1;
            }
        }

    } __except(Counter) {
        if (Counter == 2) {
            Counter += 1;
        }
    }

    if (Counter != 3) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Simple try that calls a function which raises an exception.
    //

    printf("    test8...");
    Counter = 0;
    __try {
        Counter += 1;
        foo1(STATUS_ACCESS_VIOLATION);

    } __except((GetExceptionCode() == STATUS_ACCESS_VIOLATION) ?
             EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        Counter += 1;
    }

    if (Counter != 2) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Simple try that calls a function which raises an exception.
    //

    printf("    test9...");
    Counter = 0;
    __try {
        Counter += 1;
        foo2(BlackHole, BadAddress, FALSE);

    } __except((GetExceptionCode() == STATUS_ACCESS_VIOLATION) ?
             EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        Counter += 1;
    }

    if (Counter != 2) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Simple try that calls a function which calls a function that
    // raises an exception. The first function has a finally clause
    // that must be executed for this test to work.
    //

    printf("    test10...");
    Counter = 0;
    __try {
        bar1(STATUS_ACCESS_VIOLATION, &Counter);

    } __except((GetExceptionCode() == STATUS_ACCESS_VIOLATION) ?
             EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        Counter -= 1;
    }

    if (Counter != 98) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Simple try that calls a function which calls a function that
    // raises an exception. The first function has a finally clause
    // that must be executed for this test to work.
    //

    printf("    test11...");
    Counter = 0;
    __try {
        bar2(BlackHole, BadAddress, &Counter);

    } __except((GetExceptionCode() == STATUS_ACCESS_VIOLATION) ?
             EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        Counter -= 1;
    }

    if (Counter != 98) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // A try within an except
    //

    printf("    test12...");
    Counter = 0;
    __try {
        foo1(STATUS_ACCESS_VIOLATION);

    } __except((GetExceptionCode() == STATUS_ACCESS_VIOLATION) ?
             EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        Counter += 1;
        __try {
            foo1(STATUS_SUCCESS);

        } __except((GetExceptionCode() == STATUS_SUCCESS) ?
                 EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            if (Counter != 1) {
                printf("failed, count = %d\n", Counter);

            } else {
                printf("succeeded...");
            }

            Counter += 1;
        }
    }

    if (Counter != 2) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // A try within an except
    //

    printf("    test13...");
    Counter = 0;
    __try {
        foo2(BlackHole, BadAddress, FALSE);

    } __except((GetExceptionCode() == STATUS_ACCESS_VIOLATION) ?
             EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        Counter += 1;
        __try {
            foo1(STATUS_SUCCESS);

        } __except((GetExceptionCode() == STATUS_SUCCESS) ?
                 EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            if (Counter != 1) {
                printf("failed, count = %d\n", Counter);

            } else {
                printf("succeeded...");
            }

            Counter += 1;
        }
    }

    if (Counter != 2) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // A goto from an exception clause that needs to pass
    // through a finally
    //

    printf("    test14...");
    Counter = 0;
    __try {
        __try {
            foo1(STATUS_ACCESS_VIOLATION);

        } __except((GetExceptionCode() == STATUS_ACCESS_VIOLATION) ?
                 EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            Counter += 1;
            goto t9;
        }

    } __finally {
        Counter += 1;
    }

t9:;
    if (Counter != 2) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // A goto from an finally clause that needs to pass
    // through a finally
    //

    printf("    test15...");
#if !defined(BAIL_IN_FINALLY)
    printf("skipped\n");
#else
    Counter = 0;
    __try {
        __try {
            Counter += 1;

        } __finally {
            Counter += 1;
            goto t10;
        }

    } __finally {
        Counter += 1;
    }

t10:;
    if (Counter != 3) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }
#endif

    //
    // A goto from an exception clause that needs to pass
    // through a finally into the outer finally clause.
    //

    printf("    test16...");
#if !defined(BAIL_IN_FINALLY)
    printf("skipped\n");
#else
    Counter = 0;
    __try {
        __try {
            __try {
                Counter += 1;
                foo1(STATUS_INTEGER_OVERFLOW);

            } __except(EXCEPTION_EXECUTE_HANDLER) {
                Counter += 1;
                goto t11;
            }

        } __finally {
            Counter += 1;
        }
t11:;
    } __finally {
        Counter += 1;
    }

    if (Counter != 4) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }
#endif

    //
    // A goto from an finally clause that needs to pass
    // through a finally into the outer finally clause.
    //

    printf("    test17...");
#if !defined(BAIL_IN_FINALLY)
    printf("skipped\n");
#else
    Counter = 0;
    __try {
        __try {
            Counter += 1;

        } __finally {
            Counter += 1;
            goto t12;
        }
t12:;
    } __finally {
        Counter += 1;
    }

    if (Counter != 3) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }
#endif

    //
    // A return from an except clause
    //

    printf("    test18...");
    Counter = 0;
    __try {
        Counter += 1;
        eret(STATUS_ACCESS_VIOLATION, &Counter);

    } __finally {
        Counter += 1;
    }

    if (Counter != 4) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // A return from a finally clause
    //

    printf("    test19...");
#if !defined(BAIL_IN_FINALLY)
    printf("skipped\n");
#else
    Counter = 0;
    __try {
        Counter += 1;
        fret(&Counter);

    } __finally {
        Counter += 1;
    }

    if (Counter != 5) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }
#endif

    //
    // A simple set jump followed by a long jump.
    //

    printf("    test20...");
    Counter = 0;
    if (setjmp(JumpBuffer) == 0) {
        Counter += 1;
        longjmp(JumpBuffer, 1);

    } else {
        Counter += 1;
    }

    if (Counter != 2) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // A set jump followed by a long jump out of a finally clause that is
    // sequentially executed.
    //

    printf("    test21...");
    Counter = 0;
    if (setjmp(JumpBuffer) == 0) {
        __try {
            Counter += 1;

        } __finally {
            Counter += 1;
            longjmp(JumpBuffer, 1);
        }

    } else {
        Counter += 1;
    }

    if (Counter != 3) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // A set jump within a try clause followed by a long jump out of a
    // finally clause that is sequentially executed.
    //

    printf("    test22...");
    Counter = 0;
    __try {
        if (setjmp(JumpBuffer) == 0) {
            Counter += 1;

        } else {
            Counter += 1;
        }

    } __finally {
        Counter += 1;
        if (Counter == 2) {
            Counter += 1;
            longjmp(JumpBuffer, 1);
        }
    }

    if (Counter != 5) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // A set jump followed by a try/except, followed by a try/finally where
    // the try body of the try/finally raises an exception that is handled
    // by the try/excecpt which causes the try/finally to do a long jump out
    // of a finally clause. This will create a collided unwind.
    //

    printf("    test23...");
    Counter = 0;
    if (setjmp(JumpBuffer) == 0) {
        __try {
            __try {
                Counter += 1;
                RaiseStatus(STATUS_INTEGER_OVERFLOW);

            } __finally {
                Counter += 1;
                longjmp(JumpBuffer, 1);
            }

        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Counter += 1;
        }

    } else {
        Counter += 1;
    }

    if (Counter != 3) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // A set jump followed by a try/except, followed by a several nested
    // try/finally's where the inner try body of the try/finally raises an
    // exception that is handled by the try/except which causes the
    // try/finally to do a long jump out of a finally clause. This will
    // create a collided unwind.
    //

    printf("    test24...");
    Counter = 0;
    if (setjmp(JumpBuffer) == 0) {
        __try {
            __try {
                __try {
                    __try {
                        Counter += 1;
                        RaiseStatus(STATUS_INTEGER_OVERFLOW);

                    } __finally {
                        Counter += 1;
                    }

                } __finally {
                    Counter += 1;
                    longjmp(JumpBuffer, 1);
                }

            } __finally {
                Counter += 1;
            }

        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Counter += 1;
        }

    } else {
        Counter += 1;
    }

    if (Counter != 5) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // A set jump followed by a try/except, followed by a try/finally which
    // calls a subroutine which contains a try finally that raises an
    // exception that is handled to the try/except.
    //

    printf("    test25...");
    Counter = 0;
    if (setjmp(JumpBuffer) == 0) {
        __try {
            __try {
                __try {
                    Counter += 1;
                    dojump(JumpBuffer, &Counter);

                } __finally {
                    Counter += 1;
                }

            } __finally {
                Counter += 1;
            }

        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Counter += 1;
        }

    } else {
        Counter += 1;
    }

    if (Counter != 7) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // A set jump followed by a try/except, followed by a try/finally which
    // calls a subroutine which contains a try finally that raises an
    // exception that is handled to the try/except.
    //

    printf("    test26...");
    Counter = 0;
    if (setjmp(JumpBuffer) == 0) {
        __try {
            __try {
                __try {
                    __try {
                        Counter += 1;
                        dojump(JumpBuffer, &Counter);

                    } __finally {
                        Counter += 1;
                    }

                } __finally {
                    Counter += 1;
                    longjmp(JumpBuffer, 1);
                }

            } __finally {
                Counter += 1;
            }

        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Counter += 1;
        }

    } else {
        Counter += 1;
    }

    if (Counter != 8) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Test nested exceptions.
    //

    printf("    test27...");
    Counter = 0;
    __try {
        __try {
            Counter += 1;
            except1(&Counter);

        } __except(except2(GetExceptionInformation(), &Counter)) {
            Counter += 2;
        }

    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Counter += 3;
    }

    if (Counter != 55) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Simple try that causes an integer overflow exception.
    //

    printf("    test28...");
    Counter = 0;
    __try {
        Counter += 1;
        addtwo(0x7fff0000, 0x10000, &Counter);

    } __except((GetExceptionCode() == STATUS_INTEGER_OVERFLOW) ?
             EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        Counter += 1;
    }

    if (Counter != 2) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Simple try that raises an misaligned data exception.
    //

    printf("    test29...");
#if defined(DO_ALIGNMENT_TEST)
    Counter = 0;
    __try {
        Counter += 1;
        foo2(BlackHole, (PLONG)BadByte, TRUE);

    } __except((GetExceptionCode() == STATUS_DATATYPE_MISALIGNMENT) ?
             EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        Counter += 1;
    }

    if (Counter != 2) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }
#else
    printf("skipped\n");
#endif

    //
    // Continue from a try body with an exception clause in a loop.
    //

    printf("    test30...");
    Counter = 0;
    for (Index1 = 0; Index1 < 10; Index1 += 1) {
        __try {
            if ((Index1 & 0x1) == 0) {
                continue;

            } else {
                Counter += 1;
            }

        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Counter += 40;
        }

        Counter += 2;
    }

    if (Counter != 15) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Continue from a try body with an finally clause in a loop.
    //

    printf("    test31...");
    Counter = 0;
    for (Index1 = 0; Index1 < 10; Index1 += 1) {
        __try {
            if ((Index1 & 0x1) == 0) {
                continue;

            } else {
                Counter += 1;
            }

        } __finally {
            Counter += 2;
        }

        Counter += 3;
    }

    if (Counter != 40) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Continue from doubly nested try body with an exception clause in a
    // loop.
    //

    printf("    test32...");
    Counter = 0;
    for (Index1 = 0; Index1 < 10; Index1 += 1) {
        __try {
            __try {
                if ((Index1 & 0x1) == 0) {
                    continue;

                } else {
                    Counter += 1;
                }

            } __except(EXCEPTION_EXECUTE_HANDLER) {
                Counter += 10;
            }

            Counter += 2;

        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Counter += 20;
        }

        Counter += 3;
    }

    if (Counter != 30) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Continue from doubly nested try body with an finally clause in a loop.
    //

    printf("    test33...");
    Counter = 0;
    for (Index1 = 0; Index1 < 10; Index1 += 1) {
        __try {
            __try {
                if ((Index1 & 0x1) == 0) {
                    continue;

                } else {
                    Counter += 1;
                }

            } __finally {
                Counter += 2;
            }

            Counter += 3;

        } __finally {
            Counter += 4;
        }

        Counter += 5;
    }

    if (Counter != 105) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Continue from a finally clause in a loop.
    //

    printf("    test34...");
#if !defined(BAIL_IN_FINALLY)
    printf("skipped\n");
#else
    Counter = 0;
    for (Index1 = 0; Index1 < 10; Index1 += 1) {
        __try {
            if ((Index1 & 0x1) == 0) {
                Counter += 1;
            }

        } __finally {
            Counter += 2;
            continue;
        }

        Counter += 4;
    }

    if (Counter != 25) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }
#endif

    //
    // Continue from a doubly nested finally clause in a loop.
    //

    printf("    test35...");
#if !defined(BAIL_IN_FINALLY)
    printf("skipped\n");
#else
    Counter = 0;
    for (Index1 = 0; Index1 < 10; Index1 += 1) {
        __try {
            __try {
                if ((Index1 & 0x1) == 0) {
                    Counter += 1;
                }

            } __finally {
                Counter += 2;
                continue;
            }

            Counter += 4;

        } __finally {
            Counter += 5;
        }

        Counter += 6;
    }

    if (Counter != 75) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }
#endif

    //
    // Continue from a doubly nested finally clause in a loop.
    //

    printf("    test36...");
#if !defined(BAIL_IN_FINALLY)
    printf("skipped\n");
#else
    Counter = 0;
    for (Index1 = 0; Index1 < 10; Index1 += 1) {
        __try {
            __try {
                if ((Index1 & 0x1) == 0) {
                    Counter += 1;
                }

            } __finally {
                Counter += 2;
            }

            Counter += 4;

        } __finally {
            Counter += 5;
            continue;
        }

        Counter += 6;
    }

    if (Counter != 115) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }
#endif

    //
    // Break from a try body with an exception clause in a loop.
    //

    printf("    test37...");
    Counter = 0;
    for (Index1 = 0; Index1 < 10; Index1 += 1) {
        __try {
            if ((Index1 & 0x1) == 1) {
                break;

            } else {
                Counter += 1;
            }

        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Counter += 40;
        }

        Counter += 2;
    }

    if (Counter != 3) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Break from a try body with an finally clause in a loop.
    //

    printf("    test38...");
    Counter = 0;
    for (Index1 = 0; Index1 < 10; Index1 += 1) {
        __try {
            if ((Index1 & 0x1) == 1) {
                break;

            } else {
                Counter += 1;
            }

        } __finally {
            Counter += 2;
        }

        Counter += 3;
    }

    if (Counter != 8) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Break from doubly nested try body with an exception clause in a
    // loop.
    //

    printf("    test39...");
    Counter = 0;
    for (Index1 = 0; Index1 < 10; Index1 += 1) {
        __try {
            __try {
                if ((Index1 & 0x1) == 1) {
                    break;

                } else {
                    Counter += 1;
                }

            } __except(EXCEPTION_EXECUTE_HANDLER) {
                Counter += 10;
            }

            Counter += 2;

        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Counter += 20;
        }

        Counter += 3;
    }

    if (Counter != 6) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Break from doubly nested try body with an finally clause in a loop.
    //

    printf("    test40...");
    Counter = 0;
    for (Index1 = 0; Index1 < 10; Index1 += 1) {
        __try {
            __try {
                if ((Index1 & 0x1) == 1) {
                    break;

                } else {
                    Counter += 1;
                }

            } __finally {
                Counter += 2;
            }

            Counter += 3;

        } __finally {
            Counter += 4;
        }

        Counter += 5;
    }

    if (Counter != 21) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Break from a finally clause in a loop.
    //

    printf("    test41...");
#if !defined(BAIL_IN_FINALLY)
    printf("skipped\n");
#else
    Counter = 0;
    for (Index1 = 0; Index1 < 10; Index1 += 1) {
        __try {
            if ((Index1 & 0x1) == 1) {
                Counter += 1;
            }

        } __finally {
            Counter += 2;
            break;
        }

        Counter += 4;
    }

    if (Counter != 2) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }
#endif

    //
    // Break from a doubly nested finally clause in a loop.
    //

    printf("    test42...");
#if !defined(BAIL_IN_FINALLY)
    printf("skipped\n");
#else
    Counter = 0;
    for (Index1 = 0; Index1 < 10; Index1 += 1) {
        __try {
            __try {
                if ((Index1 & 0x1) == 1) {
                    Counter += 1;
                }

            } __finally {
                Counter += 2;
                break;
            }

            Counter += 4;

        } __finally {
            Counter += 5;
        }

        Counter += 6;
    }

    if (Counter != 7) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }
#endif

    //
    // Break from a doubly nested finally clause in a loop.
    //

    printf("    test43...");
#if !defined(BAIL_IN_FINALLY)
    printf("skipped\n");
#else
    Counter = 0;
    for (Index1 = 0; Index1 < 10; Index1 += 1) {
        __try {
            __try {
                if ((Index1 & 0x1) == 1) {
                    Counter += 1;
                }

            } __finally {
                Counter += 2;
            }

            Counter += 4;

        } __finally {
            Counter += 5;
            break;
        }

        Counter += 6;
    }

    if (Counter != 11) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }
#endif

    //
    // Break from a try body with an exception clause in a switch.
    //

    printf("    test44...");
    Counter = 0;
    Index1 = 1;
    switch (Index2) {
    case BLUE:
        Counter += 100;
        break;

    case RED:
        __try {
            if ((Index1 & 0x1) == 1) {
                break;

            } else {
                Counter += 1;
            }

        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Counter += 40;
        }

        Counter += 2;
        break;
    }

    if (Counter != 0) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Break from a try body with an finally clause in a switch.
    //

    printf("    test45...");
    Counter = 0;
    Index1 = 1;
    switch (Index2) {
    case BLUE:
        Counter += 100;
        break;

    case RED:
        __try {
            if ((Index1 & 0x1) == 1) {
                break;

            } else {
                Counter += 1;
            }

        } __finally {
            Counter += 2;
        }

        Counter += 3;
    }

    if (Counter != 2) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Break from doubly nested try body with an exception clause in a
    // switch.
    //

    printf("    test46...");
    Counter = 0;
    Index1 = 1;
    switch (Index2) {
    case BLUE:
        Counter += 100;
        break;

    case RED:
        __try {
            __try {
                if ((Index1 & 0x1) == 1) {
                    break;

                } else {
                    Counter += 1;
                }

            } __except(EXCEPTION_EXECUTE_HANDLER) {
                Counter += 10;
            }

            Counter += 2;

        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Counter += 20;
        }

        Counter += 3;
    }

    if (Counter != 0) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Break from doubly nested try body with an finally clause in a switch.
    //

    printf("    test47...");
    Counter = 0;
    Index1 = 1;
    switch (Index2) {
    case BLUE:
        Counter += 100;
        break;

    case RED:
        __try {
            __try {
                if ((Index1 & 0x1) == 1) {
                    break;

                } else {
                    Counter += 1;
                }

            } __finally {
                Counter += 2;
            }

            Counter += 3;

        } __finally {
            Counter += 4;
        }

        Counter += 5;
    }

    if (Counter != 6) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Break from a finally clause in a switch.
    //

    printf("    test48...");
#if !defined(BAIL_IN_FINALLY)
    printf("skipped\n");
#else
    Counter = 0;
    Index1 = 1;
    switch (Index2) {
    case BLUE:
        Counter += 100;
        break;

    case RED:
        __try {
            if ((Index1 & 0x1) == 1) {
                Counter += 1;
            }

        } __finally {
            Counter += 2;
            break;
        }

        Counter += 4;
    }

    if (Counter != 3) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }
#endif

    //
    // Break from a doubly nested finally clause in a switch.
    //

    printf("    test49...");
#if !defined(BAIL_IN_FINALLY)
    printf("skipped\n");
#else
    Counter = 0;
    Index1 = 1;
    switch (Index2) {
    case BLUE:
        Counter += 100;
        break;

    case RED:
        __try {
            __try {
                if ((Index1 & 0x1) == 1) {
                    Counter += 1;
                }

            } __finally {
                Counter += 2;
                break;
            }

            Counter += 4;

        } __finally {
            Counter += 5;
        }

        Counter += 6;
    }

    if (Counter != 8) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }
#endif

    //
    // Break from a doubly nested finally clause in a switch.
    //

    printf("    test50...");
#if !defined(BAIL_IN_FINALLY)
    printf("skipped\n");
#else
    Counter = 0;
    Index1 = 1;
    switch (Index2) {
    case BLUE:
        Counter += 100;
        break;

    case RED:
        __try {
            __try {
                if ((Index1 & 0x1) == 1) {
                    Counter += 1;
                }

            } __finally {
                Counter += 2;
            }

            Counter += 4;

        } __finally {
            Counter += 5;
            break;
        }

        Counter += 6;
    }

    if (Counter != 12) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }
#endif

    //
    // Leave from an if in a simple try/finally.
    //

    printf("    test51...");
    Counter = 0;
    __try {
        if (Echo(Counter) == Counter) {
            Counter += 3;
            __leave;

        } else {
            Counter += 100;
        }

    } __finally {
        if (abnormal_termination() == FALSE) {
            Counter += 5;
        }
    }

    if (Counter != 8) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Leave from a loop in a simple try/finally.
    //

    printf("    test52...");
    Counter = 0;
    __try {
        for (Index1 = 0; Index1 < 10; Index1 += 1) {
            if (Echo(Index1) == Index1) {
                Counter += 3;
                __leave;
            }

            Counter += 100;
        }

    } __finally {
        if (abnormal_termination() == FALSE) {
            Counter += 5;
        }
    }

    if (Counter != 8) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Leave from a switch in a simple try/finally.
    //

    printf("    test53...");
    Counter = 0;
    __try {
        switch (Index2) {
        case BLUE:
            break;

        case RED:
            Counter += 3;
            __leave;
        }

        Counter += 100;

    } __finally {
        if (abnormal_termination() == FALSE) {
            Counter += 5;
        }
    }

    if (Counter != 8) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Leave from an if in doubly nested try/finally followed by a leave
    // from an if in the outer try/finally.
    //

    printf("    test54...");
    Counter = 0;
    __try {
        __try {
            if (Echo(Counter) == Counter) {
                Counter += 3;
                __leave;

            } else {
                Counter += 100;
            }

        } __finally {
            if (abnormal_termination() == FALSE) {
                Counter += 5;
            }
        }

        if (Echo(Counter) == Counter) {
            Counter += 3;
            __leave;

         } else {
            Counter += 100;
         }


    } __finally {
        if (abnormal_termination() == FALSE) {
            Counter += 5;
        }
    }

    if (Counter != 16) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Leave from an if in doubly nested try/finally followed by a leave
    // from the finally of the outer try/finally.
    //

    printf("    test55...");
#if !defined(BAIL_IN_FINALLY)
    printf("skipped\n");
#else
    Counter = 0;
    __try {
        __try {
            if (Echo(Counter) == Counter) {
                Counter += 3;
                __leave;

            } else {
                Counter += 100;
            }

        } __finally {
            if (abnormal_termination() == FALSE) {
                Counter += 5;
                __leave;
            }
        }

        Counter += 100;

    } __finally {
        if (abnormal_termination() == FALSE) {
            Counter += 5;
        }
    }

    if (Counter != 13) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }
#endif

    //
    // Try/finally within the except clause of a try/except that is always
    // executed.
    //

    printf("    test56...");
    Counter = 0;
    __try {
        Counter += 1;
        RaiseStatus(STATUS_INTEGER_OVERFLOW);

    } __except(Counter) {
        __try {
            Counter += 3;

        } __finally {
            if (abnormal_termination() == FALSE) {
                Counter += 5;
            }
        }
    }

    if (Counter != 9) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Try/finally within the finally clause of a try/finally.
    //

    printf("    test57...");
    Counter = 0;
    __try {
        Counter += 1;

    } __finally {
        if (abnormal_termination() == FALSE) {
            __try {
                Counter += 3;

            } __finally {
                if (abnormal_termination() == FALSE) {
                    Counter += 5;
                }
            }
        }
    }

    if (Counter != 9) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Try/except within the finally clause of a try/finally.
    //

    printf("    test58...");
#if !defined(NEST_IN_FINALLY)
    printf("skipped\n");
#else
    Counter = 0;
    __try {
        Counter -= 1;

    } __finally {
        __try {
            Counter += 2;
            RaiseStatus(STATUS_INTEGER_OVERFLOW);

        } __except(Counter) {
            __try {
                Counter += 3;

            } __finally {
                if (abnormal_termination() == FALSE) {
                    Counter += 5;
                }
            }
        }
    }

    if (Counter != 9) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

#endif  /* def(NEST_IN_FINALLY) */

    //
    // Try/except within the except clause of a try/except that is always
    // executed.
    //

    printf("    test59...");
    Counter = 0;
    __try {
        Counter += 1;
        RaiseStatus(STATUS_INTEGER_OVERFLOW);

    } __except(Counter) {
        __try {
            Counter += 3;
            RaiseStatus(STATUS_INTEGER_OVERFLOW);

        } __except(Counter - 3) {
            Counter += 5;
        }
    }

    if (Counter != 9) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Try with a Try which exits the scope with a goto
    //

    printf("    test60...");
    Counter = 0;
    __try {
        __try {
            goto outside;

        } __except(1) {
            Counter += 1;
        }

outside:
    RaiseStatus(STATUS_INTEGER_OVERFLOW);

    } __except(1) {
        Counter += 3;
    }

    if (Counter != 3) {
        printf("failed, count = %d\n", Counter);
    } else {
        printf("succeeded\n");
    }

    //
    // Try/except which gets an exception from a subfunction within
    // a try/finally which has a try/except in the finally clause
    //

    printf("    test61...");
#if !defined(NEST_IN_FINALLY)
    printf("skipped\n");
#else
    Counter = 0;
    __try {
        Test61Part2(&Counter);

    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Counter += 11;
    }

    if (Counter != 24) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }
#endif  /* def(NEST_IN_FINALLY) */


    //
    // Check for precision of exception on floating point
    //
    /* enable floating point overflow */

    printf("    test62...");
#if defined(DO_FLOAT_EXCEPTION_TEST)
    //
    // use portable version of:
    //      _control87(_control87(0,0) & ~EM_OVERFLOW, _MCW_EM);
    //

    {
        int PreviousControlFp;
        double doubleresult;

        _controlfp_s(&PreviousControlFp, 0, 0);
        _controlfp_s(&PreviousControlFp, PreviousControlFp & ~EM_OVERFLOW, _MCW_EM);

        Counter = 0;
        __try {
            doubleresult = SquareDouble (1.7e300);

            __try {
                doubleresult = SquareDouble (1.0);

            } __except(1) {
                Counter += 3;
            }

        } __except(1) {
            Counter += 1;
        }

        if (Counter != 1) {
            printf("failed, count = %d\n", Counter);

        } else {
            printf("succeeded\n");
        }

        _controlfp_s(&PreviousControlFp, PreviousControlFp, _MCW_EM);
    }

#else
    printf("skipped\n");
#endif

    //
    // A try/finally inside a try/except where an exception is raised in the
    // try/finally.
    //

    printf("    test63...");
    Counter = 0;
    __try {
        __try {
            Counter += 1;
            
        } __finally {
            Counter += 3;
            RaiseStatus(STATUS_INTEGER_OVERFLOW);
        }

    } __except(1) {
        Counter += 6;
    }

    if (Counter != 10) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // A try/finally inside a try/except where an exception is raised in the
    // in the try/except and the try/finally.
    //

    printf("    test64...");
    Counter = 0;
    __try {
        __try {
            Counter += 1;
            RaiseStatus(STATUS_INTEGER_OVERFLOW);
            
        } __finally {
            Counter += 3;
            RaiseStatus(STATUS_INTEGER_OVERFLOW);
        }

    } __except(1) {
        Counter += 6;
    }

    if (Counter != 10) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // A try/finally inside a try/except where an exception is raised in the
    // try/finally.
    //

    printf("    test65...");
    Counter = 0;
    __try {
        __try {
            Counter += 1;
            
        } __finally {
            Counter += 3;
            *BlackHole += *BadAddress;
            Counter += 13;
        }

    } __except(1) {
        Counter += 6;
    }

    if (Counter != 10) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // A try/finally inside a try/except where an exception is raised in the
    // in the try/except and the try/finally.
    //

    printf("    test66...");
    Counter = 0;
    __try {
        __try {
            Counter += 1;
            *BlackHole += *BadAddress;
            Counter += 13;
            
        } __finally {
            Counter += 3;
            *BlackHole += *BadAddress;
            Counter += 13;
        }

    } __except(1) {
        Counter += 6;
    }

    if (Counter != 10) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // A try/finally inside a try/finally inside a try/except where an
    // exception is raised in the in the try/except and in try/finally.
    //

    printf("    test67...");
    __try {
        __try {
            *BlackHole += *BadAddress;
            
        } __finally {
            __try {
                Counter = 0;

            } __finally {
                if (Counter != 0) {
                    Counter += 1;
                }
            }

            Counter += 1;
            *BlackHole += *BadAddress;
        }

    } __except(1) {
        Counter += 1;
    }

    if (Counter != 2) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // A try/finally inside a try/finally inside a try/except where an
    // exception is raised in the in the try/except and in try/finally.
    //

    printf("    test68...");
    __try {
        __try {
            RaiseStatus(STATUS_INTEGER_OVERFLOW);
            
        } __finally {
            __try {
                Counter = 0;

            } __finally {
                if (Counter != 0) {
                    Counter += 1;
                }
            }

            Counter += 1;
            RaiseStatus(STATUS_INTEGER_OVERFLOW);
        }

    } __except(1) {
        Counter += 1;
    }

    if (Counter != 2) {
        printf("failed, count = %d\n", Counter);

    } else {
        printf("succeeded\n");
    }

    //
    // Patch guard tests.
    //

    PgTests();

    ExTests();

    //
    // Announce end of exception test.
    //

    printf("End of exception test\n");
    return 0;
}

#pragma warning(pop)





