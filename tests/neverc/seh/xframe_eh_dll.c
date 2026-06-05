/*++

Copyright (c) Microsoft Corporation

Module Name:

    xframe_eh_dll.c

Abstract:

     This module implements the functions needed to test EH across different
     modules.

--*/

#include <windows.h>

typedef void (*PASS_THROUGH_DESTINATION)();

ULONG64 *test_accumulator_address;
volatile BOOLEAN break_on_each_step = FALSE;

volatile int sehR = 0xA5;
int * volatile sehD = NULL;

#define TEST_STEP(___PRIME)                         \
{                                                   \
    if (break_on_each_step) {                       \
        __debugbreak();                             \
    }                                               \
                                                    \
    *test_accumulator_address *= (___PRIME);        \
}

void setup_data_dll(int *fp, ULONG64 *taa, BOOLEAN boes)
{
    sehD = fp;
    test_accumulator_address = taa;
    break_on_each_step = boes;
}



int
EvalFilter_dll (int Disposition)
{
    TEST_STEP(7)
    return Disposition;
}


void pass_through_3_dll(PASS_THROUGH_DESTINATION dest) {
    (*dest)();
}

void pass_through_2_dll(PASS_THROUGH_DESTINATION dest, int Disposition) {
    __try
    {
        __try
        {
            pass_through_3_dll(dest);
        }
        __finally
        {
            TEST_STEP(11)
        }
    }
    __except (EvalFilter_dll(Disposition))
    {
        TEST_STEP(29)
    }
}

void pass_through_dll(PASS_THROUGH_DESTINATION dest) {
    pass_through_2_dll(dest, EXCEPTION_CONTINUE_SEARCH);
}

void pass_through_catch_dll(PASS_THROUGH_DESTINATION dest) {
    pass_through_2_dll(dest, EXCEPTION_EXECUTE_HANDLER);
}


void raise_nocatch_fin_dll(void) {
    __try
    {
        __try
        {
            RaiseException(EXCEPTION_INT_DIVIDE_BY_ZERO, 0, 0, NULL);
        }
        __finally
        {
            TEST_STEP(13)
        }
    }
    __except (EvalFilter_dll(EXCEPTION_CONTINUE_SEARCH))
    {
        TEST_STEP(31)
    }
}

void fault_and_catch_dll(void) {
    __try
    {
        sehR = *sehD;
    }
    __except (EvalFilter_dll(EXCEPTION_EXECUTE_HANDLER))
    {
        TEST_STEP(37)
    }
}

void fault_nocatch_fin(void) {
    __try
    {
        __try
        {
            sehR = *sehD;
        }
        __finally
        {
            TEST_STEP(17)
        }
    }
    __except (EvalFilter_dll(EXCEPTION_CONTINUE_SEARCH))
    {
        TEST_STEP(41)
    }
}
