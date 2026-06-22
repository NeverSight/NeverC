/*++

Copyright (c) Microsoft Corporation

Module Name:

    xframe_eh_exe.c

Abstract:

     This module implements the functions needed to test EH across different
     modules.

--*/

#include <windows.h>
#include <stdio.h>

typedef void (*PASS_THROUGH_DESTINATION)();
typedef void (*PASS_THROUGH_FUNCTION)(PASS_THROUGH_DESTINATION dest);
typedef void (*SETUP_DATA_FUNCTION)(int *fp, ULONG64 *taa, BOOLEAN boes);

PASS_THROUGH_FUNCTION gPassThrough;
PASS_THROUGH_FUNCTION gPassThroughCatch;
PASS_THROUGH_DESTINATION gRaiseNoCatchFin;
PASS_THROUGH_DESTINATION gFaultAndCatch;
PASS_THROUGH_DESTINATION gFaultNoCatchFin;

ULONG64 test_accumulator;
ULONG64 *test_accumulator_address = &test_accumulator;
volatile BOOLEAN break_on_each_step = FALSE;

volatile int sehR = 0xA5;
int * volatile sehD = NULL;

#define START_TEST                                  \
{                                                   \
    if (break_on_each_step) {                       \
        __debugbreak();                             \
    }                                               \
                                                    \
    *test_accumulator_address = 1;                  \
}

#define TEST_STEP(___PRIME)                         \
{                                                   \
    if (break_on_each_step) {                       \
        __debugbreak();                             \
    }                                               \
                                                    \
    *test_accumulator_address *= (___PRIME);        \
}

#define END_TEST(___ANSWER)                         \
{                                                   \
    if (break_on_each_step) {                       \
        __debugbreak();                             \
    }                                               \
                                                    \
    if (*test_accumulator_address != (___ANSWER)) { \
        __debugbreak();                             \
    }                                               \
}

VOID
LoadXframeDll()
{
    HMODULE hXframeDll;
    SETUP_DATA_FUNCTION setup_fault_function;

    hXframeDll = LoadLibraryExW(L"xframe_eh_dll.dll", NULL, 0);
    if (hXframeDll == NULL)
        __debugbreak();

    gPassThrough = (PASS_THROUGH_FUNCTION)GetProcAddress(hXframeDll, "pass_through_dll");
    if (gPassThrough == NULL)
        __debugbreak();

    gPassThroughCatch = (PASS_THROUGH_FUNCTION)GetProcAddress(hXframeDll, "pass_through_catch_dll");
    if (gPassThroughCatch == NULL)
        __debugbreak();

    gRaiseNoCatchFin = (PASS_THROUGH_DESTINATION)GetProcAddress(hXframeDll, "raise_nocatch_fin_dll");
    if (gRaiseNoCatchFin == NULL)
        __debugbreak();

    gFaultAndCatch = (PASS_THROUGH_DESTINATION)GetProcAddress(hXframeDll, "fault_and_catch_dll");
    if (gFaultAndCatch == NULL)
        __debugbreak();

    gFaultNoCatchFin = (PASS_THROUGH_DESTINATION)GetProcAddress(hXframeDll, "fault_nocatch_fin");
    if (gFaultNoCatchFin == NULL)
        __debugbreak();

    setup_fault_function = (SETUP_DATA_FUNCTION)GetProcAddress(hXframeDll, "setup_data_dll");
    if (setup_fault_function == NULL)
        __debugbreak();

    (*setup_fault_function)(sehD, test_accumulator_address, break_on_each_step);

    return;
}


int
EvalFilter1 (int nExcept, LPEXCEPTION_POINTERS pExcept)
{
    UNREFERENCED_PARAMETER(nExcept);
    UNREFERENCED_PARAMETER(pExcept);

    TEST_STEP(3)
    return EXCEPTION_EXECUTE_HANDLER;
}

int
EvalFilter2 (int nExcept, LPEXCEPTION_POINTERS pExcept)
{
    UNREFERENCED_PARAMETER(nExcept);
    UNREFERENCED_PARAMETER(pExcept);

    TEST_STEP(5)
    return EXCEPTION_CONTINUE_SEARCH;
}

void no_fault(void) {
    TEST_STEP(43)
}

void fault(void) {
    sehR = *sehD;
}

void fault_and_catch(void) {
    __try
    {
        sehR = *sehD;
    }
    __except (EvalFilter1(GetExceptionCode(), GetExceptionInformation()))
    {
        TEST_STEP(47);
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
            TEST_STEP(19)
        }
    }
    __except (EvalFilter2(GetExceptionCode(), GetExceptionInformation()))
    {
        TEST_STEP(53)
    }
}

void raise_nocatch_fin(void) {
    __try
    {
        __try
        {
            RaiseException(EXCEPTION_INT_DIVIDE_BY_ZERO, 0, 0, NULL);
        }
        __finally
        {
            TEST_STEP(23)
        }
    }
    __except (EvalFilter2(GetExceptionCode(), GetExceptionInformation()))
    {
        TEST_STEP(59)
    }
}

void pass_through_catch_to_fault_nocatch_fin(void) {
    (*gFaultNoCatchFin)();
}

void catch_combinations(void) {

    START_TEST
    __try
    {
        (*gPassThrough)(no_fault);
    }
    __except (EvalFilter1(GetExceptionCode(), GetExceptionInformation()))
    {
        TEST_STEP(61)
    }
    END_TEST(43 * 11)

    START_TEST
    __try
    {
        (*gPassThrough)(raise_nocatch_fin);
    }
    __except (EvalFilter1(GetExceptionCode(), GetExceptionInformation()))
    {
        TEST_STEP(61)
    }
    END_TEST(5 * 7 * 3 * 23 * 11 * 61)

    START_TEST
    __try
    {
        (*gPassThrough)(raise_nocatch_fin);
    }
    __except (EvalFilter1(GetExceptionCode(), GetExceptionInformation()))
    {
        TEST_STEP(61)
    }
    END_TEST(5 * 7 * 3 * 23 * 11 * 61)

    START_TEST
    __try
    {
        (*gPassThrough)(fault_nocatch_fin);
    }
    __except (EvalFilter1(GetExceptionCode(), GetExceptionInformation()))
    {
        TEST_STEP(61)
    }
    END_TEST(5 * 7 * 3 * 19 * 11 * 61)

    START_TEST
    __try
    {
        (*gPassThroughCatch)(raise_nocatch_fin);
    }
    __except (EvalFilter1(GetExceptionCode(), GetExceptionInformation()))
    {
        TEST_STEP(61)
    }
    END_TEST(5 * 7 * 23 * 11 * 29)

    START_TEST
    __try
    {
        (*gPassThroughCatch)(fault_nocatch_fin);
    }
    __except (EvalFilter1(GetExceptionCode(), GetExceptionInformation()))
    {
        TEST_STEP(61)
    }
    END_TEST(5 * 7 * 19 * 11 * 29)

    START_TEST
    __try
    {
        (*gFaultAndCatch)();
    }
    __except (EvalFilter1(GetExceptionCode(), GetExceptionInformation()))
    {
        TEST_STEP(61)
    }
    END_TEST(7 * 37)

    START_TEST
    __try
    {
        (*gPassThroughCatch)(pass_through_catch_to_fault_nocatch_fin);
    }
    __except (EvalFilter1(GetExceptionCode(), GetExceptionInformation()))
    {
        TEST_STEP(61)
    }
    END_TEST(7 * 7 * 17 * 11 * 29)

    START_TEST
    __try
    {
        (*gPassThrough)(pass_through_catch_to_fault_nocatch_fin);
    }
    __except (EvalFilter1(GetExceptionCode(), GetExceptionInformation()))
    {
        TEST_STEP(61)
    }
    END_TEST(7 * 7 * 3 * 17 * 11 * 61)

}

void AllocateFaultPage(void)
{

    VOID *FaultPage;
    
    FaultPage = VirtualAlloc
    (
        NULL,
        sizeof(int),
        MEM_RESERVE,
        PAGE_READWRITE
    );

    if (FaultPage == NULL)
    {
        TEST_STEP(71)
    }

    sehD = FaultPage;

    return;
}

void CommitFaultPage(void)
{

    VOID *FaultPage;

    FaultPage = VirtualAlloc
    (
        sehD,
        sizeof(int),
        MEM_COMMIT,
        PAGE_READWRITE
    );

    if (FaultPage != sehD)
    {
        TEST_STEP(73)
    }
}

void DecommitFaultPage(void)
{
    VirtualFree
    (
        sehD,
        sizeof(int),
        MEM_DECOMMIT
    );
}

int
EvalFilter3 (int nExcept, LPEXCEPTION_POINTERS pExcept)
{
    TEST_STEP(67)

    if ((nExcept == STATUS_ACCESS_VIOLATION) &&
        (pExcept->ExceptionRecord->ExceptionInformation[1] == (ULONG_PTR)sehD))
    {
        CommitFaultPage();

        return EXCEPTION_CONTINUE_EXECUTION;
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

void resume_combinations(void)
{

    START_TEST
    __try
    {
        (*gPassThrough)(fault_nocatch_fin);
    }
    __except (EvalFilter3(GetExceptionCode(), GetExceptionInformation()))
    {
        TEST_STEP(61)
    }
    DecommitFaultPage();
    END_TEST(5 * 7 * 67 * 19 * 11)

    START_TEST
    __try
    {
        (*gFaultNoCatchFin)();
    }
    __except (EvalFilter3(GetExceptionCode(), GetExceptionInformation()))
    {
        TEST_STEP(61)
    }
    DecommitFaultPage();
    END_TEST(7 * 67 * 17)

}

int
__cdecl
main(void)
{
    AllocateFaultPage();
    LoadXframeDll();

    catch_combinations();
    puts("Caught Exceptions Test PASSED.");

    resume_combinations();
    puts("Resumed Exceptions Test PASSED.");

    return 0;
}

