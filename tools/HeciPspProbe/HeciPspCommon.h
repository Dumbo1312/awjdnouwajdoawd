/**
 * HeciPspCommon.h — shared types (no EDK2, raw MSVC)
 */
#ifndef HECI_PSP_COMMON_H
#define HECI_PSP_COMMON_H

typedef unsigned char      UINT8;
typedef unsigned char      BOOLEAN;
typedef unsigned short     UINT16;
typedef unsigned int       UINT32;
typedef unsigned long long UINT64;
typedef unsigned long long UINTN;
typedef unsigned long long size_t;
typedef unsigned short     CHAR16;
typedef void               VOID;
typedef VOID              *EFI_HANDLE;
typedef UINT64             EFI_STATUS;
typedef UINT64             EFI_PHYSICAL_ADDRESS;

#define EFIAPI __cdecl

#define EFI_SUCCESS           0ULL
#define EFI_NOT_FOUND         14ULL
#define EFI_NOT_READY         6ULL
#define EFI_TIMEOUT           18ULL
#define EFI_OUT_OF_RESOURCES  9ULL
#define EFI_ERROR(S)          ((S) != EFI_SUCCESS)

#define EFI_VARIABLE_NON_VOLATILE        0x00000001U
#define EFI_VARIABLE_BOOTSERVICE_ACCESS  0x00000002U
#define EFI_VARIABLE_RUNTIME_ACCESS      0x00000004U

#define COM1  0x3F8U

/* Result status codes */
#define PROBE_STATUS_OK            0U
#define PROBE_STATUS_ME_NOT_READY  1U
#define PROBE_STATUS_RESET_FAILED  2U
#define PROBE_STATUS_NOT_READY     3U
#define PROBE_STATUS_SEND_FAILED   4U
#define PROBE_STATUS_RECV_FAILED   5U
#define PROBE_STATUS_SMN_OK        6U
#define PROBE_STATUS_NOT_FOUND     7U
#define PROBE_STATUS_ALLOC_FAILED  8U
#define PROBE_STATUS_CMD_TIMEOUT   9U

typedef struct {
  EFI_GUID VendorGuid;
  VOID    *VendorTable;
} EFI_CONFIGURATION_TABLE;

typedef struct { UINT32 D1; UINT16 D2; UINT16 D3; UINT8 D4[8]; } EFI_GUID;
typedef struct { UINT64 Sig; UINT32 Rev; UINT32 HdrSz; UINT32 Crc; UINT32 Rsvd; } EFI_TABLE_HEADER;

typedef EFI_STATUS(EFIAPI *EFI_SET_VARIABLE)(
    CHAR16 *Name, EFI_GUID *Guid, UINT32 Attr, UINTN Sz, VOID *Data);
typedef EFI_STATUS(EFIAPI *EFI_ALLOCATE_PAGES)(
    UINT32 Type, UINT32 MemType, UINTN Pages, EFI_PHYSICAL_ADDRESS *Mem);
typedef EFI_STATUS(EFIAPI *EFI_FREE_PAGES)(
    EFI_PHYSICAL_ADDRESS Mem, UINTN Pages);

typedef enum { AllocateAnyPages = 0, AllocateMaxAddress = 1, AllocateAddress = 2 } EFI_ALLOCATE_TYPE;
#define EfiBootServicesData 4U

typedef struct {
  EFI_TABLE_HEADER Hdr;
  VOID *RaiseTPL; VOID *RestoreTPL;
  EFI_ALLOCATE_PAGES AllocatePages;
  EFI_FREE_PAGES     FreePages;
  VOID *GetMemoryMap;
  VOID *AllocatePool; VOID *FreePool;
  VOID *CreateEvent;  VOID *SetTimer;
  VOID *WaitForEvent; VOID *SignalEvent;
  VOID *CloseEvent;   VOID *CheckEvent;
  VOID *IPx4[8];
  VOID *LocateHandle; VOID *LocateDevicePath;
  VOID *InstallConfigurationTable;
  VOID *LoadImage;    VOID *StartImage;
  VOID *Exit;         VOID *UnloadImage;
  VOID *ExitBootServices;
  VOID *GetNextMonotonicCount; VOID *Stall;
  VOID *SetWatchdogTimer;
  VOID *ConnectController; VOID *DisconnectController;
  VOID *OpenProtocol; VOID *CloseProtocol;
  VOID *OpenProtocolInformation;
  VOID *ProtocolsPerHandle; VOID *LocateHandleBuffer;
  VOID *LocateProtocol;
  VOID *InstallMultipleProtocolInterfaces;
  VOID *UninstallMultipleProtocolInterfaces;
  VOID *CalculateCrc32; VOID *CopyMem; VOID *SetMem;
  VOID *CreateEventEx;
} EFI_BOOT_SERVICES;

typedef struct {
  EFI_TABLE_HEADER Hdr;
  VOID *GetTime; VOID *SetTime; VOID *GetWakeupTime; VOID *SetWakeupTime;
  VOID *SetVirtualAddressMap; VOID *ConvertPointer;
  VOID *GetVariable; VOID *GetNextVariableName;
  EFI_SET_VARIABLE SetVariable;
} EFI_RUNTIME_SERVICES;

typedef struct {
  EFI_TABLE_HEADER     Hdr;
  CHAR16              *FirmwareVendor;
  UINT32               FirmwareRevision;
  EFI_HANDLE           ConsoleInHandle;
  VOID                *ConIn;
  EFI_HANDLE           ConsoleOutHandle;
  VOID                *ConOut;
  EFI_HANDLE           StdErrHandle;
  VOID                *StdErr;
  EFI_RUNTIME_SERVICES *RuntimeServices;
  EFI_BOOT_SERVICES   *BootServices;
  UINTN                NumberOfTableEntries;
  EFI_CONFIGURATION_TABLE *ConfigurationTable;
} EFI_SYSTEM_TABLE;

#pragma pack(push,1)
/* Result stored in UEFI variable L"HECIResult" */
typedef struct {
  UINT32 Status;       /* PROBE_STATUS_* */
  UINT32 HCsr;
  UINT32 MeCsr;
  UINT16 FwMajor; UINT16 FwMinor;
  UINT16 FwBuild; UINT16 FwHotfix;
} HECI_RESULT;

/* Result stored in UEFI variable L"PSPResult" */
typedef struct {
  UINT32 Status;       /* PROBE_STATUS_* */
  UINT32 SmnMbox;
  UINT32 FeatReg;
  UINT32 StsReg;
  UINT32 CmdRetCode;
  UINT8  PspReady;
  UINT8  Recovery;
  UINT8  Pad[2];
} PSP_RESULT;
#pragma pack(pop)

static VOID ZeroMem(VOID *Buf, UINTN Sz) {
  UINT8 *P = (UINT8 *)Buf; while (Sz--) *P++ = 0;
}

#endif /* HECI_PSP_COMMON_H */
