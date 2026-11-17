/** @file
  HeciPspProbe.c

  DXE driver that auto-detects Intel vs AMD platform and opens a
  communication channel to Intel ME (via HECI circular-buffer mailbox)
  or AMD PSP (via CCP/PSP PCI BAR2 mailbox, with SMN fallback).

  Results are stored in a UEFI runtime variable so they survive to OS:
    Guid  = {DEADBEEF-1234-5678-AA-BB-CC-DD-EE-FF-00-11}
    Name  = L"MEVersion"  (Intel)  or  L"PSPStatus"  (AMD)

  Build:  drop into any EDK2 package, add to the DSC, build as DXE_DRIVER.
  Flash:  add the resulting .efi to DXE volume via UEFITool / MMTool.
**/

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/PciLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/TimerLib.h>
#include <Library/BaseLib.h>

// ---------------------------------------------------------------------------
// Result variable GUID
// ---------------------------------------------------------------------------

STATIC EFI_GUID mProbeGuid = {
  0xDEADBEEF, 0x1234, 0x5678,
  { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11 }
};

STATIC VOID
StoreVar (CHAR16 *Name, VOID *Data, UINTN Size)
{
  gRT->SetVariable (
    Name, &mProbeGuid,
    EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS |
    EFI_VARIABLE_NON_VOLATILE,
    Size, Data
  );
}

// ---------------------------------------------------------------------------
// Platform detection
// ---------------------------------------------------------------------------

STATIC BOOLEAN
IsAmdPlatform (VOID)
{
  UINT32 Ebx;
  AsmCpuid (0, NULL, &Ebx, NULL, NULL);
  return (Ebx == 0x68747541); // "Auth" from "AuthenticAMD"
}

// ===========================================================================
// INTEL ME  —  HECI (Host Embedded Controller Interface)
// ===========================================================================
//
// Communication model:
//   Host and ME each own a circular buffer in the HECI MMIO window.
//   Each direction: a 32-bit transport header (HECI_MSG_HDR) followed
//   by payload DWORDs.  The MKHI layer sits on top.
//
// HECI MMIO layout (BAR0):
//   0x00  H_CSR     — Host control/status  (host R/W)
//   0x08  ME_CB_RW  — ME → Host read window (host reads ME output here)
//   0x0C  ME_CSR_HA — ME control/status    (host reads only)
//   0x10  H_CB_WW   — Host → ME write window (host writes here)
//
// CSR layout (same for H_CSR and ME_CSR_HA):
//   [0]      IE   — Interrupt Enable
//   [1]      IS   — Interrupt Status (write-1-to-clear)
//   [2]      IG   — Interrupt Generate (doorbell)
//   [3]      RDY  — Ready
//   [4]      RST  — Reset
//   [15:8]   CBWP — Circular Buffer Write Pointer
//   [23:16]  CBRP — Circular Buffer Read Pointer
//   [31:24]  CBD  — Circular Buffer Depth (slots, power of 2)
// ---------------------------------------------------------------------------

#define HECI_H_CSR      0x00
#define HECI_ME_CB_RW   0x08
#define HECI_ME_CSR_HA  0x0C
#define HECI_H_CB_WW    0x10

#define CSR_IE   BIT0
#define CSR_IS   BIT1
#define CSR_IG   BIT2
#define CSR_RDY  BIT3
#define CSR_RST  BIT4

#define HECI_TIMEOUT_US  (2000 * 1000ULL)  // 2 s

// Known HECI PCI device IDs (vendor 0x8086, function 0)
STATIC UINT16 mHeciDevIds[] = {
  0x1C3A, 0x1E3A,               // 6/7-series
  0x8C3A, 0x8CBA, 0x8D3A,       // 8/9/C610-series
  0xA13A, 0xA2BA, 0xA33A,       // 100/200/300-series
  0x9D3A, 0x9DE0,               // 7th/8th-gen mobile
  0x02E0, 0x06E0, 0x43E0,       // Ice Lake / Comet / Tiger Lake
  0xA0E0, 0x51E0, 0xA780,       // TGL-H / ADL / RPL
  0x7AE8,                       // Meteor Lake
  0x0000
};

#pragma pack(1)

// Transport header written/read to/from the circular buffer
typedef union {
  UINT32 Raw;
  struct {
    UINT32 MEAddr   :  8;  // ME client address
    UINT32 HostAddr :  8;  // Host client address (0 = BIOS)
    UINT32 Length   :  9;  // Payload byte count
    UINT32 Rsvd     :  6;
    UINT32 Complete :  1;  // Last (or only) fragment
  } F;
} HECI_MSG_HDR;

// MKHI message header (first 4 bytes of every MKHI payload)
typedef struct {
  UINT8 GroupId;
  UINT8 Command;   // bit7 = IsResponse flag
  UINT8 Reserved;
  UINT8 Result;    // 0 = success (responses only)
} MKHI_HDR;

// GET_FW_VERSION (GroupId=0xFF, Cmd=0x02) response
typedef struct {
  MKHI_HDR Mkhi;
  UINT16   CodeMinor;
  UINT16   CodeMajor;
  UINT16   CodeBuild;
  UINT16   CodeHotfix;
  UINT16   RcvyMinor;
  UINT16   RcvyMajor;
  UINT16   RcvyBuild;
  UINT16   RcvyHotfix;
} ME_FW_VER_RSP;

#pragma pack()

#define MKHI_GRP_GEN        0xFF
#define MKHI_CMD_GET_VER    0x02
// ME-side MKHI fixed-address client (most platforms 6–13th gen)
#define HECI_ME_MKHI_ADDR   0x27

STATIC UINTN mHeciMmio;

STATIC UINT32 HR (UINT32 r) { return MmioRead32  (mHeciMmio + r); }
STATIC VOID   HW (UINT32 r, UINT32 v) { MmioWrite32 (mHeciMmio + r, v); }

STATIC EFI_STATUS
HeciWaitHostReady (VOID)
{
  UINT64 T = HECI_TIMEOUT_US;
  while (T) {
    if ((HR (HECI_ME_CSR_HA) & CSR_RDY) && (HR (HECI_H_CSR) & CSR_RDY))
      return EFI_SUCCESS;
    MicroSecondDelay (100); T -= 100;
  }
  return EFI_TIMEOUT;
}

STATIC EFI_STATUS
HeciSend (VOID *Payload, UINT32 Len, UINT8 MeAddr)
{
  HECI_MSG_HDR Hdr;
  UINT32 *D = (UINT32 *)Payload;
  UINT32  Dwords = (Len + 3) / 4;
  UINT64  T = HECI_TIMEOUT_US;

  // Wait for free slots in host CB
  while (T) {
    UINT32 Csr  = HR (HECI_H_CSR);
    UINT8  Dep  = (UINT8)(Csr >> 24);
    UINT8  Wp   = (UINT8)(Csr >> 8);
    UINT8  Rp   = (UINT8)(Csr >> 16);
    UINT8  Free = Dep - ((UINT8)(Wp - Rp));
    if (Free >= Dwords + 1) break; // +1 for header slot
    MicroSecondDelay (100); T -= 100;
  }
  if (!T) return EFI_TIMEOUT;

  Hdr.Raw = 0;
  Hdr.F.MEAddr   = MeAddr;
  Hdr.F.HostAddr = 0;
  Hdr.F.Length   = Len;
  Hdr.F.Complete = 1;

  HW (HECI_H_CB_WW, Hdr.Raw);
  for (UINT32 i = 0; i < Dwords; i++)
    HW (HECI_H_CB_WW, D[i]);

  // Ring the doorbell
  HW (HECI_H_CSR, HR (HECI_H_CSR) | CSR_IG);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
HeciRecv (VOID *Buf, UINT32 BufSize, UINT32 *OutLen)
{
  UINT64 T = HECI_TIMEOUT_US;

  // Wait for ME to put data in its CB
  while (T) {
    UINT32 MeCsr = HR (HECI_ME_CSR_HA);
    if ((UINT8)(MeCsr >> 8) != (UINT8)(MeCsr >> 16)) break; // WP != RP
    MicroSecondDelay (100); T -= 100;
  }
  if (!T) return EFI_TIMEOUT;

  HECI_MSG_HDR Hdr;
  Hdr.Raw = HR (HECI_ME_CB_RW);
  UINT32 Len    = Hdr.F.Length;
  UINT32 Dwords = (Len + 3) / 4;
  if (Len > BufSize) return EFI_BUFFER_TOO_SMALL;

  UINT32 *D = (UINT32 *)Buf;
  for (UINT32 i = 0; i < Dwords; i++)
    D[i] = HR (HECI_ME_CB_RW);

  // Acknowledge interrupt
  HW (HECI_H_CSR, HR (HECI_H_CSR) | CSR_IS);

  *OutLen = Len;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
HeciGetFwVersion (ME_FW_VER_RSP *Out)
{
  MKHI_HDR Req = { MKHI_GRP_GEN, MKHI_CMD_GET_VER, 0, 0 };
  UINT32   RLen;
  EFI_STATUS S;

  S = HeciSend (&Req, sizeof Req, HECI_ME_MKHI_ADDR);
  if (EFI_ERROR (S)) return S;
  return HeciRecv (Out, sizeof *Out, &RLen);
}

STATIC UINTN
FindAndEnableHeci (VOID)
{
  for (UINTN Bus = 0; Bus < 8; Bus++) {
    for (UINTN Dev = 0; Dev < 32; Dev++) {
      UINT32 Id = PciRead32 (PCI_LIB_ADDRESS (Bus, Dev, 0, 0));
      if ((Id & 0xFFFF) != 0x8086) continue;
      UINT16 Did = (UINT16)(Id >> 16);
      for (UINT32 i = 0; mHeciDevIds[i]; i++) {
        if (mHeciDevIds[i] != Did) continue;
        // Enable MMIO decode
        UINT16 Cmd = PciRead16 (PCI_LIB_ADDRESS (Bus, Dev, 0, 4));
        PciWrite16 (PCI_LIB_ADDRESS (Bus, Dev, 0, 4), Cmd | BIT1);
        UINT32 Bar = PciRead32 (PCI_LIB_ADDRESS (Bus, Dev, 0, 0x10)) & ~0xFU;
        Print (L"  HECI @ PCI %02X:%02X.0  DevId=%04X  BAR0=%08X\n",
               Bus, Dev, Did, Bar);
        return Bar;
      }
    }
  }
  return 0;
}

STATIC EFI_STATUS
RunIntelPath (VOID)
{
  mHeciMmio = FindAndEnableHeci ();
  if (!mHeciMmio) {
    Print (L"  HECI device not found on PCI bus 0-7\n");
    return EFI_NOT_FOUND;
  }

  EFI_STATUS S = HeciWaitHostReady ();
  if (EFI_ERROR (S)) {
    Print (L"  HECI not ready: H_CSR=%08X ME_CSR=%08X\n",
           HR (HECI_H_CSR), HR (HECI_ME_CSR_HA));
    return S;
  }
  Print (L"  HECI ready  H_CSR=%08X\n", HR (HECI_H_CSR));

  ME_FW_VER_RSP Ver;
  S = HeciGetFwVersion (&Ver);
  if (EFI_ERROR (S)) {
    Print (L"  GET_FW_VERSION failed: %r  (try ME addr 0x00 or 0x07)\n", S);
    // Note: if 0x27 doesn't work, the ME MKHI client address may differ
    // on this platform.  Enumerate via HECI_CLIENT_ENUMERATE (Cmd=0x00).
    return S;
  }

  Print (L"  ME Code   : %d.%d.%d.%d\n",
         Ver.CodeMajor,  Ver.CodeMinor,
         Ver.CodeBuild,  Ver.CodeHotfix);
  Print (L"  ME Rcvy   : %d.%d.%d.%d\n",
         Ver.RcvyMajor,  Ver.RcvyMinor,
         Ver.RcvyBuild,  Ver.RcvyHotfix);

  StoreVar (L"MEVersion", &Ver, sizeof Ver);
  return EFI_SUCCESS;
}

// ===========================================================================
// AMD PSP  —  PCI BAR2 mailbox  (CCP/PSP PCI device)  +  SMN fallback
// ===========================================================================
//
// AMD exposes the PSP via the CCP (Cryptographic Co-Processor) PCI device.
// The host-to-PSP mailbox lives in BAR2:
//
//   Zen 2/3 (confirmed from Linux ccp driver):
//     BAR2 + 0x10500  : Feature register
//     BAR2 + 0x10570  : x86→PSP command register
//                         [31]    = command valid / READY
//                         [30:0]  = physical address of command buffer
//     BAR2 + 0x10574  : PSP→x86 response register
//                         [31]    = PSP done
//                         [0]     = pending
//                         [1]     = error
//
//  Command buffer layout (physical RAM, below 4 GB):
//     UINT32  TotalSize   — total buffer size in bytes
//     UINT32  CommandId   — PSP command code
//     UINT32  ReturnCode  — filled by PSP on completion
//     ...     arguments
//
//  SMN fallback (when PCI device not found / Zen 1):
//     I/O port 0xCD8 = SMN index,  0xCDC = SMN data
//     Mailbox register at SMN address 0x13B10028
// ---------------------------------------------------------------------------

STATIC UINT16 mPspDevIds[] = {
  0x1456,  // Zen   (Naples / Summit Ridge)
  0x1468,  // Zen+  (Pinnacle / TR2)
  0x1486,  // Zen 2 (Rome / Matisse)
  0x1649,  // Zen 3 (Milan / Vermeer / Cezanne)
  0x14CA,  // Zen 4 (Genoa / Raphael / Phoenix)
  0x0000
};

// BAR2 register offsets (Zen 2 / Zen 3 — adjust for Zen 1 or Zen 4 if needed)
#define PSP_FEAT_REG    0x10500
#define PSP_CMD_REG     0x10570
#define PSP_STS_REG     0x10574

#define PSP_CMD_VALID   BIT31
#define PSP_STS_DONE    BIT31
#define PSP_STS_PEND    BIT0
#define PSP_STS_ERR     BIT1

#define SMN_IDX   0xCD8
#define SMN_DAT   0xCDC
#define PSP_SMN_MBOX   0x13B10028UL

#pragma pack(1)
typedef struct {
  UINT32 TotalSize;
  UINT32 CommandId;
  UINT32 ReturnCode;
  UINT32 Reserved;
  // command-specific arguments follow
} PSP_CMD_BUF;
#pragma pack()

STATIC UINTN mPspBar2;

STATIC UINT32
SmnRead (UINT32 Addr)
{
  IoWrite32 (SMN_IDX, Addr);
  return IoRead32 (SMN_DAT);
}

STATIC UINTN
FindPspBar2 (VOID)
{
  for (UINTN Bus = 0; Bus < 8; Bus++) {
    for (UINTN Dev = 0; Dev < 32; Dev++) {
      UINT32 Id = PciRead32 (PCI_LIB_ADDRESS (Bus, Dev, 0, 0));
      if ((Id & 0xFFFF) != 0x1022) continue;
      UINT16 Did = (UINT16)(Id >> 16);
      for (UINT32 i = 0; mPspDevIds[i]; i++) {
        if (mPspDevIds[i] != Did) continue;
        // BAR2 at config offset 0x18; check for 64-bit BAR
        UINT32 Bar32 = PciRead32 (PCI_LIB_ADDRESS (Bus, Dev, 0, 0x18));
        UINT64 Bar   = Bar32 & ~0xFULL;
        if ((Bar32 & 0x6) == 0x4) {
          UINT32 Hi = PciRead32 (PCI_LIB_ADDRESS (Bus, Dev, 0, 0x1C));
          Bar |= ((UINT64)Hi << 32);
        }
        // Enable MMIO decode
        UINT16 Cmd = PciRead16 (PCI_LIB_ADDRESS (Bus, Dev, 0, 4));
        PciWrite16 (PCI_LIB_ADDRESS (Bus, Dev, 0, 4), Cmd | BIT1);
        Print (L"  PSP/CCP @ PCI %02X:%02X.0  DevId=%04X  BAR2=%016LX\n",
               Bus, Dev, Did, Bar);
        return (UINTN)Bar;
      }
    }
  }
  return 0;
}

STATIC EFI_STATUS
PspSendCmd (UINT32 CmdId, VOID *ArgData, UINT32 ArgSize)
{
  EFI_STATUS          S;
  EFI_PHYSICAL_ADDRESS PA = 0xFFFFFFFF;
  UINTN               Pages;
  PSP_CMD_BUF        *Buf;
  UINT32              Total;

  Total = sizeof (PSP_CMD_BUF) + ArgSize;
  Pages = EFI_SIZE_TO_PAGES (Total);

  S = gBS->AllocatePages (AllocateMaxAddress, EfiBootServicesData, Pages, &PA);
  if (EFI_ERROR (S)) return S;

  Buf = (PSP_CMD_BUF *)(UINTN)PA;
  ZeroMem (Buf, Total);
  Buf->TotalSize = Total;
  Buf->CommandId = CmdId;
  if (ArgData && ArgSize)
    CopyMem ((UINT8 *)Buf + sizeof *Buf, ArgData, ArgSize);

  // Write PA with VALID bit set → PSP picks up the command
  MmioWrite32 (mPspBar2 + PSP_CMD_REG, (UINT32)PA | PSP_CMD_VALID);

  // Poll until PSP clears PENDING
  UINT64 T = 5000 * 1000ULL;
  while (T) {
    UINT32 Sts = MmioRead32 (mPspBar2 + PSP_STS_REG);
    if (!(Sts & PSP_STS_PEND)) break;
    MicroSecondDelay (100); T -= 100;
  }

  UINT32 Rc = Buf->ReturnCode;
  UINT32 Sts = MmioRead32 (mPspBar2 + PSP_STS_REG);
  Print (L"  PSP Cmd 0x%08X  ReturnCode=0x%08X  STS=%08X  %s\n",
         CmdId, Rc, Sts,
         (Sts & PSP_STS_ERR) ? L"ERROR" : (T ? L"OK" : L"TIMEOUT"));

  gBS->FreePages (PA, Pages);
  return T ? EFI_SUCCESS : EFI_TIMEOUT;
}

STATIC EFI_STATUS
RunAmdPath (VOID)
{
  mPspBar2 = FindPspBar2 ();

  if (!mPspBar2) {
    Print (L"  PSP PCI device not found, trying SMN fallback\n");
    UINT32 Mbox = SmnRead (PSP_SMN_MBOX);
    Print (L"  SMN[0x13B10028] = 0x%08X\n", Mbox);
    if (Mbox == 0xFFFFFFFF) {
      Print (L"  SMN read returned 0xFFFFFFFF — not accessible\n");
      return EFI_NOT_FOUND;
    }
    Print (L"  PSP via SMN  READY=%d  RECOVERY=%d  CMD_RESP=0x%02X\n",
           (Mbox >> 31) & 1, (Mbox >> 30) & 1, Mbox & 0xFF);

    UINT32 Status[2] = { Mbox, 0 };
    StoreVar (L"PSPStatus", Status, sizeof Status);
    return EFI_SUCCESS;
  }

  UINT32 FeatReg = MmioRead32 (mPspBar2 + PSP_FEAT_REG);
  UINT32 StsReg  = MmioRead32 (mPspBar2 + PSP_STS_REG);
  Print (L"  PSP Feature Reg : 0x%08X\n", FeatReg);
  Print (L"  PSP Status Reg  : 0x%08X\n", StsReg);
  Print (L"  PSP Done        : %d\n", (StsReg & PSP_STS_DONE) ? 1 : 0);

  UINT32 StatusData[2] = { FeatReg, StsReg };
  StoreVar (L"PSPStatus", StatusData, sizeof StatusData);

  //
  // Send a minimal probe command.
  //
  // IMPORTANT: Choose a safe read-only command for initial probing.
  // CommandId 0x00000013 is PSP_CMD_GET_FW_VERSION on Zen 2/3.
  // On other gens it may differ — check AMD PSP firmware symbols or
  // Linux ccp driver source (psp-dev.c) for your platform.
  //
  // DO NOT send PSP_CMD_ENTER_RECOVERY (0x00000001) or any write command
  // unless you know what you are doing.
  //
  Print (L"  Sending PSP GET_FW_VERSION probe (CmdId=0x13)...\n");
  PspSendCmd (0x00000013, NULL, 0);

  return EFI_SUCCESS;
}

// ===========================================================================
// Entry point
// ===========================================================================

EFI_STATUS
EFIAPI
HeciPspProbeEntry (
  IN EFI_HANDLE       ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
)
{
  EFI_STATUS S;

  Print (L"\n[HeciPspProbe] ----------------------------------------\n");

  if (IsAmdPlatform ()) {
    Print (L"Platform : AMD\n");
    S = RunAmdPath ();
  } else {
    Print (L"Platform : Intel\n");
    S = RunIntelPath ();
  }

  Print (L"[HeciPspProbe] Done  Status=%r\n\n", S);
  return S;
}
