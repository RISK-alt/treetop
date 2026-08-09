#pragma once

/*                                  INCLUDES                                  */

# include <windows.h>

/*                           INFORMATION CLASSES                              */

# define SystemProcessInformation               5
# define SystemProcessorPerformanceInformation  8
# define ProcessBasicInformation                0
# define ProcessCommandLineInformation          60

/*                          SEMI-DOCUMENTED TYPES                             */

/*
** These mirror the layouts winternl.h / the DDK's ntdef.h use for the
** same names. Guarded behind _WINTERNL_ - the include guard winternl.h
** itself defines - so that if a translation unit ever pulls that header
** in too (directly, or via some future SDK header), its definitions win
** and ours simply step aside instead of colliding.
*/
#ifndef _WINTERNL_

typedef struct _UNICODE_STRING
{
    USHORT  Length;
    USHORT  MaximumLength;
    PWSTR   Buffer;
}   UNICODE_STRING, *PUNICODE_STRING;

typedef struct _SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION
{
    LARGE_INTEGER   IdleTime;
    LARGE_INTEGER   KernelTime;
    LARGE_INTEGER   UserTime;
    LARGE_INTEGER   Reserved1[2];
    ULONG           Reserved2;
}   SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION;

typedef LONG    KPRIORITY;

typedef struct _VM_COUNTERS
{
    SIZE_T  PeakVirtualSize;
    SIZE_T  VirtualSize;
    ULONG   PageFaultCount;
    SIZE_T  PeakWorkingSetSize;
    SIZE_T  WorkingSetSize;
    SIZE_T  QuotaPeakPagedPoolUsage;
    SIZE_T  QuotaPagedPoolUsage;
    SIZE_T  QuotaPeakNonPagedPoolUsage;
    SIZE_T  QuotaNonPagedPoolUsage;
    SIZE_T  PagefileUsage;
    SIZE_T  PeakPagefileUsage;
}   VM_COUNTERS;

/*
** Not used until Task 10, which walks SystemProcessInformation to build
** the process table. Declared now so every ntdll-shaped type lives here,
** in the one file that owns semi-documented API.
*/
typedef struct _SYSTEM_PROCESS_INFORMATION
{
    ULONG           NextEntryOffset;
    ULONG           NumberOfThreads;
    LARGE_INTEGER   Reserved[3];
    LARGE_INTEGER   CreateTime;
    LARGE_INTEGER   UserTime;
    LARGE_INTEGER   KernelTime;
    UNICODE_STRING  ImageName;
    KPRIORITY       BasePriority;
    HANDLE          UniqueProcessId;
    HANDLE          InheritedFromUniqueProcessId;
    ULONG           HandleCount;
    ULONG           SessionId;
    ULONG           PageDirectoryBase;
    VM_COUNTERS     VirtualMemoryCounters;
    SIZE_T          PrivatePageCount;
    IO_COUNTERS     IoCounters;
}   SYSTEM_PROCESS_INFORMATION, *PSYSTEM_PROCESS_INFORMATION;

#endif /* _WINTERNL_ */

/*                              ENTRY POINTS                                  */

int     ntapi_init(void);
int     ntapi_available(void);
LONG    nt_query_system(ULONG cls, PVOID buf, ULONG len, PULONG ret);
LONG    nt_query_process(HANDLE h, ULONG cls, PVOID buf, ULONG len,
            PULONG ret);
