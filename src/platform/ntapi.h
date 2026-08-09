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
** UNICODE_STRING and SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION are
** field-identical between the Microsoft SDK's winternl.h and
** MinGW-w64's - verified by diffing both against this declaration.
** Guarded behind _WINTERNL_, the include guard winternl.h itself
** defines, so that if a translation unit ever pulls that header in too
** (directly, or via some future SDK header), its definitions win and
** ours simply steps aside. Safe either way, since the layouts agree.
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

#endif /* _WINTERNL_ */

/*
** SYSTEM_PROCESS_INFORMATION does NOT agree across toolchains, unlike
** the two types above. The Microsoft SDK declares it opaquely -
** Reserved1[52], Reserved2[3], Reserved3, Reserved4[4], Reserved5[11],
** Reserved6[6] - with none of the named fields Task 10 needs
** (NumberOfThreads, CreateTime, UserTime, KernelTime, ImageName,
** BasePriority, SessionId, ...). MinGW-w64 exposes the named layout
** this codebase relies on. VM_COUNTERS and KPRIORITY, which this type
** depends on, are opaque on the SDK for the same reason.
**
** A "real header wins" guard here would compile fine on MinGW and fail
** every named-field access the moment MSVC sees <winternl.h> in the
** same translation unit - exactly the class of bug this file exists to
** contain. So these three get TT_-prefixed names that cannot collide
** with any SDK definition, on any toolchain, ever, and Task 10 must
** always use these names rather than the bare ntdll ones. Do not
** merge this into the guarded block above: the two groups disagree on
** whether the SDK's definition is safe to defer to.
*/
typedef LONG    TT_KPRIORITY;

typedef struct _TT_VM_COUNTERS
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
}   TT_VM_COUNTERS;

/*
** Used by Task 10, which walks SystemProcessInformation to build the
** process table. Field layout checked directly against the installed
** MinGW-w64 winternl.h (x86_64-w64-mingw32/include/winternl.h) rather
** than trusted from memory - it matches this struct exactly, Reserved[3]
** included.
**
** Reserved[3] is three LARGE_INTEGERs (24 bytes) that, since Vista,
** carry named fields MinGW's own header still lumps together:
**   Reserved[0].QuadPart        -> WorkingSetPrivateSize  (Vista+)
**   Reserved[1] low/high ULONG  -> HardFaultCount,
**                                  NumberOfThreadsHighWatermark (Win7+)
**   Reserved[2].QuadPart        -> CycleTime               (Win7+)
** Task 10 only needs the first slot. Splitting Reserved[3] into named
** fields here was considered and rejected: this exact layout is the one
** verified against the real header, and hand-deriving sub-field offsets
** for the other two slots would trade a checked struct for a guessed
** one, for fields nothing in this codebase reads.
*/
typedef struct _TT_SYSTEM_PROCESS_INFORMATION
{
    ULONG           NextEntryOffset;
    ULONG           NumberOfThreads;
    LARGE_INTEGER   Reserved[3];
    LARGE_INTEGER   CreateTime;
    LARGE_INTEGER   UserTime;
    LARGE_INTEGER   KernelTime;
    UNICODE_STRING  ImageName;
    TT_KPRIORITY    BasePriority;
    HANDLE          UniqueProcessId;
    HANDLE          InheritedFromUniqueProcessId;
    ULONG           HandleCount;
    ULONG           SessionId;
    ULONG           PageDirectoryBase;
    TT_VM_COUNTERS  VirtualMemoryCounters;
    SIZE_T          PrivatePageCount;
    IO_COUNTERS     IoCounters;
}   TT_SYSTEM_PROCESS_INFORMATION, *TT_PSYSTEM_PROCESS_INFORMATION;

/*                              ENTRY POINTS                                  */

int     ntapi_init(void);
int     ntapi_available(void);
LONG    nt_query_system(ULONG cls, PVOID buf, ULONG len, PULONG ret);
LONG    nt_query_process(HANDLE h, ULONG cls, PVOID buf, ULONG len,
            PULONG ret);
