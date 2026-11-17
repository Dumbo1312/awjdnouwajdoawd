/**
 * HeciPspProbe.c
 *
 * DXE driver that auto-detects Intel vs AMD and opens a communication
 * channel to Intel ME (via HECI) or AMD PSP (via SMN / CCP PCI BAR2).
 *
 * Build: see build.cmd  (MSVC x64, no EDK2 dependency)
 * Flash: inject as DXE_DRIVER in DXE volume (UEFITool / MMTool)
 *
 * Results are written to UEFI runtime variable so they survive to OS:
 *   Guid  = {DEADBEEF-1234-5678-AA-BB-CC-DD-EE-FF-00-11}
 *   L"HECIResult"  (Intel ME)
 *   L"PSPResult"   (AMD PSP)
 */

#include <intrin.h>
#include "HeciPspCommon.h"

#pragma intrinsic(__inbyte)
#pragma intrinsic(__outbyte)
#pragma intrinsic(__indword)
#pragma intrinsic(__outdword)
#pragma intrinsic(__cpuid)
#pragma function(memset)
#pragma function(memcpy)

VOID *memset(VOID *Dst, int Val, size_t Size) {
  UINT8 *D = (UINT8 *)Dst;
  while (Size--) *D++ = (UINT8)Val;
  return Dst;
}
VOID *memcpy(VOID *Dst, const VOID *Src, size_t Size) {
  const UINT8 *S = (const UINT8 *)Src;
  UINT8 *D = (UINT8 *)Dst;
  while (Size--) *D++ = *S++;
  return Dst;
}

static EFI_SYSTEM_TABLE *gST;

/* =========================================================================
 * Serial debug (COM1, like SmmMem)
 * ========================================================================= */

static VOID IoWait(VOID) { __outbyte(0x80, 0); }

static VOID SerialInit(VOID) {
  __outbyte(COM1 + 1, 0x00);
  __outbyte(COM1 + 3, 0x80);
  __outbyte(COM1 + 0, 0x01);
  __outbyte(COM1 + 1, 0x00);
  __outbyte(COM1 + 3, 0x03);
  __outbyte(COM1 + 2, 0xC7);
  __outbyte(COM1 + 4, 0x0B);
  IoWait();
}

static VOID PutChar(char Ch) {
  UINTN G = 100000;
  if (Ch == '\n') PutChar('\r');
  while (((__inbyte(COM1 + 5) & 0x20) == 0) && G--) {}
  __outbyte(COM1, (UINT8)Ch);
}

static VOID Log(const char *S) { while (*S) PutChar(*S++); }

static VOID LogHex(UINT64 V) {
  const char *H = "0123456789ABCDEF";
  int S = 60;
  for (;;) { PutChar(H[(V >> S) & 0xF]); if (!S) break; S -= 4; }
}

static VOID LogH32(UINT32 V) {
  const char *H = "0123456789ABCDEF";
  int S = 28;
  for (;;) { PutChar(H[(V >> S) & 0xF]); if (!S) break; S -= 4; }
}

/* =========================================================================
 * UEFI variable storage
 * ========================================================================= */

static EFI_GUID gResultGuid = {
  0xDEADBEEF, 0x1234, 0x5678,
  { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11 }
};

static VOID StoreVar(CHAR16 *Name, VOID *Data, UINTN Size) {
  UINT32 Attr = EFI_VARIABLE_NON_VOLATILE |
                EFI_VARIABLE_BOOTSERVICE_ACCESS |
                EFI_VARIABLE_RUNTIME_ACCESS;
  if (gST && gST->RuntimeServices && gST->RuntimeServices->SetVariable)
    gST->RuntimeServices->SetVariable(Name, &gResultGuid, Attr, Size, Data);
}

/* =========================================================================
 * Platform detection
 * ========================================================================= */

static BOOLEAN IsAmd(VOID) {
  int R[4];
  __cpuid(R, 0);
  return (UINT32)R[1] == 0x68747541U; /* "Auth" */
}

/* =========================================================================
 * PCI config space (CF8 / CFC)
 *
 * CF8h = 0x80000000 | (Bus<<16) | (Dev<<11) | (Fun<<8) | (Reg & 0xFC)
 * ========================================================================= */

#define PCI_ADDR(Bus, Dev, Fun, Reg) \
  (0x80000000U | ((UINT32)(Bus)<<16) | ((UINT32)(Dev)<<11) | \
   ((UINT32)(Fun)<<8) | ((Reg) & 0xFC))

static UINT32 PciRead32(UINT8 Bus, UINT8 Dev, UINT8 Fun, UINT8 Reg) {
  __outdword(0xCF8, PCI_ADDR(Bus, Dev, Fun, Reg));
  return __indword(0xCFC);
}
static UINT16 PciRead16(UINT8 Bus, UINT8 Dev, UINT8 Fun, UINT8 Reg) {
  __outdword(0xCF8, PCI_ADDR(Bus, Dev, Fun, Reg));
  return (UINT16)(__indword(0xCFC) >> ((Reg & 2) * 8));
}
static VOID PciWrite16(UINT8 Bus, UINT8 Dev, UINT8 Fun, UINT8 Reg, UINT16 Val) {
  UINT32 Cur;
  __outdword(0xCF8, PCI_ADDR(Bus, Dev, Fun, Reg));
  Cur = __indword(0xCFC);
  Cur &= ~((UINT32)0xFFFF << ((Reg & 2) * 8));
  Cur |= (UINT32)Val << ((Reg & 2) * 8);
  __outdword(0xCFC, Cur);
}

/* =========================================================================
 * MMIO helpers (volatile pointer, no EDK2)
 * ========================================================================= */

static UINT32 MmioR32(UINT64 Addr) {
  return *(volatile UINT32 *)(UINTN)Addr;
}
static VOID MmioW32(UINT64 Addr, UINT32 Val) {
  *(volatile UINT32 *)(UINTN)Addr = Val;
}

/* =========================================================================
 * Spin-wait helpers
 * ========================================================================= */

static VOID SpinUs(UINT32 Us) {
  /* Rough spin — no TimerLib dependency */
  volatile UINT32 N = Us * 300; /* ~300 iterations per µs on modern HW */
  while (N--) IoWait();
}

/* =========================================================================
 * ==========================================================================
 * INTEL ME  —  HECI
 * ==========================================================================
 *
 * HECI MMIO register layout (BAR0):
 *
 *   0x00  H_CSR     — Host control / status   (host R/W)
 *   0x08  ME_CB_RW  — ME → Host read window   (host reads ME output here)
 *   0x0C  ME_CSR_HA — ME control / status     (host read-only)
 *   0x10  H_CB_WW   — Host → ME write window  (host writes here)
 *
 * CSR bit layout (identical for H_CSR and ME_CSR_HA):
 *   [7:0]   CBD  — Circular Buffer Depth (read-only slot count)
 *   [15:8]  CBWP — Circular Buffer Write Pointer
 *   [23:16] CBRP — Circular Buffer Read Pointer
 *   [24]    IE   — Interrupt Enable
 *   [25]    IS   — Interrupt Status (write-1-to-clear)
 *   [26]    IG   — Interrupt Generate (doorbell)
 *   [27]    RDY  — Ready
 *   [28]    RST  — Reset
 *
 * HECI transport header (one DWORD, written before payload):
 *   [7:0]   ME client address
 *   [15:8]  Host client address  (0 = BIOS fixed-address client)
 *   [24:16] Payload byte count
 *   [30:25] Reserved
 *   [31]    Message complete (last fragment)
 *
 * Fixed-address MKHI client addresses (most 6th–13th gen Intel):
 *   ME address  = 0x07   (MKHI fixed client)
 *   Host address = 0x00
 *
 * MKHI GroupId 0xFF, Cmd 0x02 = GET_FW_VERSION
 * ========================================================================= */

#define H_CSR      0x00U
#define ME_CB_RW   0x08U
#define ME_CSR_HA  0x0CU
#define H_CB_WW    0x10U

/* CSR bit positions */
#define CSR_CBD_MASK   0x000000FFU  /* bits  [7:0]  */
#define CSR_CBWP_MASK  0x0000FF00U  /* bits [15:8]  */
#define CSR_CBRP_MASK  0x00FF0000U  /* bits [23:16] */
#define CSR_IE         (1U << 24)
#define CSR_IS         (1U << 25)
#define CSR_IG         (1U << 26)
#define CSR_RDY        (1U << 27)
#define CSR_RST        (1U << 28)

/* Extract CBD / CBWP / CBRP from a CSR value */
#define CSR_CBD(csr)  ((csr) & 0xFFU)
#define CSR_WP(csr)   (((csr) >> 8)  & 0xFFU)
#define CSR_RP(csr)   (((csr) >> 16) & 0xFFU)

#define HECI_TIMEOUT_US  2000000U   /* 2 s */
#define HECI_ME_ADDR     0x07U      /* fixed-address MKHI client */
#define HECI_HOST_ADDR   0x00U

#define MKHI_GRP_GEN     0xFFU
#define MKHI_CMD_VER     0x02U

static UINT64 gHeciBmm;  /* BAR0 base, 0 = not found */

#pragma pack(push,1)
typedef union {
  UINT32 Raw;
  struct {
    UINT32 MEAddr   : 8;
    UINT32 HostAddr : 8;
    UINT32 Length   : 9;
    UINT32 Rsvd     : 6;
    UINT32 Complete : 1;
  } F;
} HECI_HDR;

typedef struct { UINT8 GroupId; UINT8 Command; UINT8 Rsvd; UINT8 Result; } MKHI_HDR;

typedef struct {
  MKHI_HDR Mkhi;
  UINT16 CodeMinor; UINT16 CodeMajor; UINT16 CodeBuild; UINT16 CodeHotfix;
  UINT16 RcvyMinor; UINT16 RcvyMajor; UINT16 RcvyBuild; UINT16 RcvyHotfix;
} ME_FW_VER;
#pragma pack(pop)

static UINT32 HR(UINT32 Reg) { return MmioR32(gHeciBmm + Reg); }
static VOID   HW(UINT32 Reg, UINT32 V) { MmioW32(gHeciBmm + Reg, V); }

/* Known HECI device IDs (vendor 0x8086, Bus 0, Dev 22, Fun 0 on all platforms) */
static UINT16 kHeciDevIds[] = {
  0x1C3A, 0x1E3A,               /* 6/7-series  */
  0x8C3A, 0x8CBA, 0x8D3A,       /* 8/9/C610    */
  0xA13A, 0xA2BA, 0xA33A,       /* 100/200/300 */
  0x9D3A, 0x9DE0,               /* 7th/8th mobile */
  0x02E0, 0x06E0, 0x43E0,       /* ICL/CML/TGL */
  0xA0E0, 0x51E0, 0xA780,       /* TGL-H/ADL/RPL */
  0x7AE8, 0x7F70,               /* MTL / LNL  */
  0x0000
};

static BOOLEAN HeciIdOk(UINT16 Did) {
  UINT32 i;
  for (i = 0; kHeciDevIds[i]; i++)
    if (kHeciDevIds[i] == Did) return 1;
  return 0;
}

/* Find HECI PCI device.  HECI1 is always Bus 0 Dev 22 Fun 0 on Intel PCHs.
   We try that first; if the device ID doesn't match, do a narrow scan. */
static BOOLEAN HeciFindAndEnable(VOID) {
  UINT32 Id; UINT16 Did; UINT32 Bar; UINT8 Bus, Dev;

  /* Primary: B0:D22:F0 */
  Id = PciRead32(0, 22, 0, 0);
  if ((Id & 0xFFFF) == 0x8086 && HeciIdOk((UINT16)(Id >> 16))) {
    Bus = 0; Dev = 22;
    goto Found;
  }

  /* Fallback: scan Bus 0 Devs 20-25 */
  for (Dev = 20; Dev <= 25; Dev++) {
    Id = PciRead32(0, Dev, 0, 0);
    if ((Id & 0xFFFF) != 0x8086) continue;
    Did = (UINT16)(Id >> 16);
    if (HeciIdOk(Did)) { Bus = 0; goto Found; }
  }
  Log("heci: not found on PCI\n");
  return 0;

Found:
  Did = (UINT16)(Id >> 16);
  /* Enable MMIO decode (bit 1 of Command register, offset 4) */
  PciWrite16(Bus, Dev, 0, 4, PciRead16(Bus, Dev, 0, 4) | 0x0002);
  /* BAR0 at config offset 0x10 — 32-bit MMIO */
  Bar = PciRead32(Bus, Dev, 0, 0x10) & 0xFFFFFFF0U;
  gHeciBmm = (UINT64)Bar;
  Log("heci: B0:D"); LogH32(Dev); Log(":F0  Did="); LogH32(Did);
  Log("  BAR0="); LogH32(Bar); Log("\n");
  return Bar != 0;
}

static BOOLEAN HeciWaitReady(VOID) {
  UINT32 T = HECI_TIMEOUT_US;
  while (T > 0) {
    if ((HR(ME_CSR_HA) & CSR_RDY) && (HR(H_CSR) & CSR_RDY)) return 1;
    SpinUs(100); T -= 100;
  }
  return 0;
}

/* Reset host side and wait for ME to acknowledge / come back up.
   Call this if ME_RDY is set but H_RDY is not, or on initial setup. */
static BOOLEAN HeciReset(VOID) {
  UINT32 Csr, T;

  Log("heci: resetting host side\n");
  /* Trigger host reset */
  Csr = HR(H_CSR);
  HW(H_CSR, Csr | CSR_RST);
  SpinUs(5000);

  /* Wait for ME to notice (ME_RDY should drop) */
  T = HECI_TIMEOUT_US;
  while (T > 0) {
    if (!(HR(ME_CSR_HA) & CSR_RDY)) break;
    SpinUs(100); T -= 100;
  }

  /* Clear H_RST and assert H_RDY */
  Csr = HR(H_CSR);
  Csr &= ~CSR_RST;
  Csr |=  CSR_RDY;
  Csr |=  CSR_IS;   /* W1C — clear any stale interrupt status */
  HW(H_CSR, Csr);

  /* Wait for ME_RDY to come back */
  T = HECI_TIMEOUT_US;
  while (T > 0) {
    if (HR(ME_CSR_HA) & CSR_RDY) break;
    SpinUs(100); T -= 100;
  }
  return !!(HR(ME_CSR_HA) & CSR_RDY);
}

static BOOLEAN HeciSend(VOID *Payload, UINT32 Len, UINT8 MeAddr) {
  HECI_HDR Hdr; UINT32 *D = (UINT32 *)Payload;
  UINT32 Dwords = (Len + 3) / 4; UINT64 T = HECI_TIMEOUT_US;

  Hdr.Raw = 0;
  Hdr.F.MEAddr   = MeAddr;
  Hdr.F.HostAddr = HECI_HOST_ADDR;
  Hdr.F.Length   = Len;
  Hdr.F.Complete = 1;

  /* Wait for free host CB slots (need Dwords+1 for header) */
  while (T > 0) {
    UINT32 Csr = HR(H_CSR);
    UINT8 Dep = CSR_CBD(Csr);
    UINT8 Wp  = CSR_WP(Csr);
    UINT8 Rp  = CSR_RP(Csr);
    UINT8 Used = (UINT8)(Wp - Rp);    /* wraps naturally for 8-bit */
    UINT8 Free = (UINT8)(Dep - Used);
    if (Free >= Dwords + 1) break;
    SpinUs(100); T -= 100;
  }
  if (!T) { Log("heci: send timeout\n"); return 0; }

  HW(H_CB_WW, Hdr.Raw);
  { UINT32 i; for (i = 0; i < Dwords; i++) HW(H_CB_WW, D[i]); }

  /* Doorbell */
  HW(H_CSR, HR(H_CSR) | CSR_IG);
  return 1;
}

static BOOLEAN HeciRecv(VOID *Buf, UINT32 BufSize, UINT32 *OutLen) {
  HECI_HDR Hdr; UINT32 *D = (UINT32 *)Buf;
  UINT64 T = HECI_TIMEOUT_US;

  /* Wait for ME to write data to its CB (ME_CBWP != ME_CBRP) */
  while (T > 0) {
    UINT32 Csr = HR(ME_CSR_HA);
    if (CSR_WP(Csr) != CSR_RP(Csr)) break;
    SpinUs(100); T -= 100;
  }
  if (!T) { Log("heci: recv timeout\n"); return 0; }

  Hdr.Raw = HR(ME_CB_RW);
  *OutLen = Hdr.F.Length;

  if (Hdr.F.Length > BufSize) {
    Log("heci: recv overflow\n");
    return 0;
  }
  { UINT32 i, Dw = (Hdr.F.Length + 3) / 4;
    for (i = 0; i < Dw; i++) D[i] = HR(ME_CB_RW); }

  /* Acknowledge */
  HW(H_CSR, HR(H_CSR) | CSR_IS);
  return 1;
}

static EFI_STATUS RunIntel(VOID) {
  UINT32 HCsr, MeCsr;
  MKHI_HDR Req;
  ME_FW_VER Ver;
  UINT32 RLen;
  HECI_RESULT Result;

  ZeroMem(&Result, sizeof(Result));

  if (!HeciFindAndEnable()) return EFI_NOT_FOUND;

  HCsr  = HR(H_CSR);
  MeCsr = HR(ME_CSR_HA);
  Log("heci: H_CSR="); LogH32(HCsr); Log(" ME_CSR="); LogH32(MeCsr); Log("\n");

  Result.HCsr  = HCsr;
  Result.MeCsr = MeCsr;

  /* ME not running? */
  if (!(MeCsr & CSR_RDY)) {
    Log("heci: ME_RDY=0, ME not running or HECI disabled in ME FW\n");
    Result.Status = PROBE_STATUS_ME_NOT_READY;
    StoreVar(L"HECIResult", &Result, sizeof(Result));
    return EFI_NOT_READY;
  }

  /* Host not ready → reset */
  if (!(HCsr & CSR_RDY)) {
    if (!HeciReset()) {
      Log("heci: host reset failed, ME did not come back\n");
      Result.Status = PROBE_STATUS_RESET_FAILED;
      StoreVar(L"HECIResult", &Result, sizeof(Result));
      return EFI_TIMEOUT;
    }
    Log("heci: reset OK\n");
  }

  if (!HeciWaitReady()) {
    Log("heci: not ready after reset\n");
    Result.Status = PROBE_STATUS_NOT_READY;
    StoreVar(L"HECIResult", &Result, sizeof(Result));
    return EFI_TIMEOUT;
  }

  Log("heci: READY  CBD="); LogH32(CSR_CBD(HR(H_CSR))); Log("\n");

  /* ---- Send MKHI GET_FW_VERSION ---- */
  ZeroMem(&Req, sizeof(Req));
  Req.GroupId  = MKHI_GRP_GEN;
  Req.Command  = MKHI_CMD_VER;
  if (!HeciSend(&Req, sizeof(Req), HECI_ME_ADDR)) {
    Result.Status = PROBE_STATUS_SEND_FAILED;
    StoreVar(L"HECIResult", &Result, sizeof(Result));
    return EFI_TIMEOUT;
  }

  ZeroMem(&Ver, sizeof(Ver));
  if (!HeciRecv(&Ver, sizeof(Ver), &RLen)) {
    /* Retry once with ME addr 0x00 — some older/newer platforms differ */
    Log("heci: retry with ME addr 0x00\n");
    ZeroMem(&Req, sizeof(Req));
    Req.GroupId  = MKHI_GRP_GEN;
    Req.Command  = MKHI_CMD_VER;
    if (!HeciSend(&Req, sizeof(Req), 0x00)) {
      Result.Status = PROBE_STATUS_SEND_FAILED;
      StoreVar(L"HECIResult", &Result, sizeof(Result));
      return EFI_TIMEOUT;
    }
    ZeroMem(&Ver, sizeof(Ver));
    if (!HeciRecv(&Ver, sizeof(Ver), &RLen)) {
      Result.Status = PROBE_STATUS_RECV_FAILED;
      StoreVar(L"HECIResult", &Result, sizeof(Result));
      return EFI_TIMEOUT;
    }
  }

  Log("heci: ME FW ");
  LogH32(Ver.CodeMajor); Log("."); LogH32(Ver.CodeMinor);
  Log("."); LogH32(Ver.CodeBuild); Log("."); LogH32(Ver.CodeHotfix); Log("\n");

  Result.Status     = PROBE_STATUS_OK;
  Result.FwMajor    = Ver.CodeMajor;
  Result.FwMinor    = Ver.CodeMinor;
  Result.FwBuild    = Ver.CodeBuild;
  Result.FwHotfix   = Ver.CodeHotfix;
  Result.HCsr       = HR(H_CSR);
  Result.MeCsr      = HR(ME_CSR_HA);
  StoreVar(L"HECIResult", &Result, sizeof(Result));
  return EFI_SUCCESS;
}

/* =========================================================================
 * ==========================================================================
 * AMD PSP
 * ==========================================================================
 *
 * Two access paths tried in order:
 *
 * 1. SMN (System Management Network) indirect I/O — Zen 1 / 2 / 3:
 *      I/O 0xCD8 = SMN index register (32-bit write)
 *      I/O 0xCDC = SMN data  register (32-bit read/write)
 *      PSP mailbox at SMN address 0x13B10028:
 *        [31]    READY  — PSP sets when ready to accept commands
 *        [30]    RECOVERY — PSP in recovery mode
 *        [7:0]   command response code (PSP fills after completing cmd)
 *
 * 2. CCP/PSP PCI device BAR2 (Zen 2 / 3):
 *      Vendor 0x1022, various device IDs
 *      BAR2 register offsets (Zen 2 / 3 layout):
 *        0x10500  Feature register
 *        0x10570  x86→PSP command register  [31]=valid, [30:0]=cmdBuf PA
 *        0x10574  PSP→x86 response register [31]=done, [1]=error, [0]=pending
 *
 * PSP command buffer layout (physical RAM, must be below 4 GB):
 *      UINT32  TotalSize
 *      UINT32  CommandId
 *      UINT32  ReturnCode  (PSP writes result here)
 *      UINT32  Reserved
 *      + command-specific args
 *
 * CmdId 0x00000013 = PSP_CMD_GET_FW_VERSION (Zen 2 / 3)
 * CmdId 0x00000016 = PSP_CMD_GET_FW_VERSION (Zen 1) — try both
 * ========================================================================= */

#define SMN_INDEX    0xCD8U
#define SMN_DATA     0xCDCU
#define PSP_SMN_MBOX 0x13B10028UL

#define PSP_SMN_MBOX_ZEN4  0x13B10928UL  /* Zen 4 / Phoenix mailbox shifted */

#define PSP_BAR2_FEAT_REG  0x10500U
#define PSP_BAR2_CMD_REG   0x10570U      /* Zen 2/3 layout */
#define PSP_BAR2_STS_REG   0x10574U

#define PSP_CMD_VALID   (1U << 31)
#define PSP_STS_DONE    (1U << 31)
#define PSP_STS_ERR     (1U << 1)
#define PSP_STS_PENDING (1U << 0)

#define PSP_CMD_GET_VER_ZEN2  0x00000013U
#define PSP_CMD_GET_VER_ZEN1  0x00000016U

static UINT16 kPspDevIds[] = {
  0x1456, /* Zen   (Naples / Summit Ridge) */
  0x1468, /* Zen+  (Pinnacle / TR2) */
  0x1486, /* Zen 2 (Rome / Matisse) */
  0x1649, /* Zen 3 (Milan / Vermeer / Cezanne) */
  0x14CA, /* Zen 4 (Genoa / Raphael / Phoenix) */
  0x0000
};

#pragma pack(push,1)
typedef struct {
  UINT32 TotalSize;
  UINT32 CommandId;
  UINT32 ReturnCode;
  UINT32 Reserved;
} PSP_CMD_BUF;
#pragma pack(pop)

static UINT32 SmnRead(UINT32 Addr) {
  __outdword(SMN_INDEX, Addr);
  return __indword(SMN_DATA);
}

static BOOLEAN PspIdOk(UINT16 Did) {
  UINT32 i;
  for (i = 0; kPspDevIds[i]; i++)
    if (kPspDevIds[i] == Did) return 1;
  return 0;
}

/* Find PSP/CCP PCI device; returns BAR2 (or 0).
   AMD CCP/PSP is NOT at a fixed Bus/Dev location — scan all of Bus 0. */
static UINT64 PspFindBar2(VOID) {
  UINT8 Dev;
  for (Dev = 0; Dev < 32; Dev++) {
    UINT32 Id = PciRead32(0, Dev, 0, 0);
    if ((Id & 0xFFFF) == 0x1022 && PspIdOk((UINT16)(Id >> 16))) {
      UINT32 B32 = PciRead32(0, Dev, 0, 0x18); /* BAR2 at config 0x18 */
      UINT64 Bar = (UINT64)(B32 & 0xFFFFFFF0U);
      if ((B32 & 0x6) == 0x4) { /* 64-bit BAR */
        UINT32 Hi = PciRead32(0, Dev, 0, 0x1C);
        Bar |= ((UINT64)Hi << 32);
      }
      PciWrite16(0, Dev, 0, 4, PciRead16(0, Dev, 0, 4) | 0x0002);
      Log("psp: B0:D"); LogH32(Dev); Log(":F0  Did=");
      LogH32((UINT16)(Id >> 16)); Log("  BAR2="); LogHex(Bar); Log("\n");
      return Bar;
    }
  }
  return 0;
}

/* Allocate physical page below 4 GB via EFI boot services */
static UINT32 AllocBelow4G(UINTN Pages) {
  EFI_PHYSICAL_ADDRESS PA = 0xFFFFFFFFULL;
  if (gST && gST->BootServices && gST->BootServices->AllocatePages)
    if (!EFI_ERROR(gST->BootServices->AllocatePages(
          AllocateMaxAddress, EfiBootServicesData, Pages, &PA)))
      return (UINT32)PA;
  return 0;
}

static VOID FreePages(UINT32 PA, UINTN Pages) {
  if (gST && gST->BootServices && gST->BootServices->FreePages)
    gST->BootServices->FreePages((EFI_PHYSICAL_ADDRESS)PA, Pages);
}

static EFI_STATUS RunAmd(VOID) {
  UINT64 Bar2; UINT32 Mbox, Feat, Sts, CmdBufPa;
  PSP_CMD_BUF *Cmd; UINT64 T;
  PSP_RESULT Result;

  ZeroMem(&Result, sizeof(Result));

  /* ---- Path 1: SMN mailbox ---- */
  Mbox = SmnRead(PSP_SMN_MBOX);
  Log("psp: SMN[0x13B10028]="); LogH32(Mbox); Log("\n");

  if (Mbox == 0xFFFFFFFF) {
    /* Try Zen 4 address */
    Mbox = SmnRead((UINT32)PSP_SMN_MBOX_ZEN4);
    Log("psp: SMN[0x13B10928]="); LogH32(Mbox); Log("\n");
  }

  Result.SmnMbox = Mbox;

  if (Mbox != 0xFFFFFFFF) {
    Log("psp: READY="); LogH32((Mbox >> 31) & 1);
    Log(" RECOVERY="); LogH32((Mbox >> 30) & 1);
    Log(" RESP="); LogH32(Mbox & 0xFF); Log("\n");
    Result.Status    = PROBE_STATUS_SMN_OK;
    Result.PspReady  = (Mbox >> 31) & 1;
    Result.Recovery  = (Mbox >> 30) & 1;
  }

  /* ---- Path 2: PCI BAR2 ---- */
  Bar2 = PspFindBar2();
  if (!Bar2) {
    Log("psp: CCP/PSP PCI device not found, SMN-only result\n");
    if (Mbox == 0xFFFFFFFF) {
      Result.Status = PROBE_STATUS_NOT_FOUND;
      StoreVar(L"PSPResult", &Result, sizeof(Result));
      return EFI_NOT_FOUND;
    }
    StoreVar(L"PSPResult", &Result, sizeof(Result));
    return EFI_SUCCESS;
  }

  Feat = MmioR32(Bar2 + PSP_BAR2_FEAT_REG);
  Sts  = MmioR32(Bar2 + PSP_BAR2_STS_REG);
  Log("psp: BAR2 Feat="); LogH32(Feat); Log(" Sts="); LogH32(Sts); Log("\n");

  Result.FeatReg = Feat;
  Result.StsReg  = Sts;

  /* ---- Send a probe command via BAR2 mailbox ---- */
  CmdBufPa = AllocBelow4G(1);
  if (!CmdBufPa) {
    Log("psp: alloc failed\n");
    Result.Status = PROBE_STATUS_ALLOC_FAILED;
    StoreVar(L"PSPResult", &Result, sizeof(Result));
    return EFI_OUT_OF_RESOURCES;
  }
  Cmd = (PSP_CMD_BUF *)(UINTN)CmdBufPa;
  ZeroMem(Cmd, sizeof(*Cmd));
  Cmd->TotalSize = sizeof(*Cmd);
  Cmd->CommandId = PSP_CMD_GET_VER_ZEN2;

  /* Write command buffer PA + VALID bit to CMD register */
  MmioW32(Bar2 + PSP_BAR2_CMD_REG, CmdBufPa | PSP_CMD_VALID);

  /* Poll for PSP to clear PENDING bit (or set DONE) */
  T = (UINT64)5000 * 1000;
  while (T > 0) {
    Sts = MmioR32(Bar2 + PSP_BAR2_STS_REG);
    if (!(Sts & PSP_STS_PENDING)) break;
    SpinUs(100); T -= 100;
  }

  Sts = MmioR32(Bar2 + PSP_BAR2_STS_REG);
  Log("psp: cmd done  STS="); LogH32(Sts);
  Log(" RetCode="); LogH32(Cmd->ReturnCode); Log("\n");

  if (!T) {
    Log("psp: command timeout — trying Zen1 CmdId 0x16\n");
    ZeroMem(Cmd, sizeof(*Cmd));
    Cmd->TotalSize = sizeof(*Cmd);
    Cmd->CommandId = PSP_CMD_GET_VER_ZEN1;
    MmioW32(Bar2 + PSP_BAR2_CMD_REG, CmdBufPa | PSP_CMD_VALID);
    T = (UINT64)5000 * 1000;
    while (T > 0) {
      Sts = MmioR32(Bar2 + PSP_BAR2_STS_REG);
      if (!(Sts & PSP_STS_PENDING)) break;
      SpinUs(100); T -= 100;
    }
    Sts = MmioR32(Bar2 + PSP_BAR2_STS_REG);
    Log("psp: zen1 retry  STS="); LogH32(Sts);
    Log(" RetCode="); LogH32(Cmd->ReturnCode); Log("\n");
  }

  Result.Status    = T ? PROBE_STATUS_OK : PROBE_STATUS_CMD_TIMEOUT;
  Result.CmdRetCode = Cmd->ReturnCode;
  Result.StsReg    = Sts;

  FreePages(CmdBufPa, 1);
  StoreVar(L"PSPResult", &Result, sizeof(Result));
  return T ? EFI_SUCCESS : EFI_TIMEOUT;
}

/* =========================================================================
 * Entry point
 * ========================================================================= */

EFI_STATUS EFIAPI ProbeEntry(EFI_HANDLE ImageHandle,
                             EFI_SYSTEM_TABLE *SystemTable) {
  EFI_STATUS S;
  (void)ImageHandle;

  gST = SystemTable;
  SerialInit();

  Log("\n[HeciPspProbe] ----------------------------------------\n");

  if (IsAmd()) {
    Log("Platform: AMD\n");
    S = RunAmd();
  } else {
    Log("Platform: Intel\n");
    S = RunIntel();
  }

  Log("[HeciPspProbe] Done\n\n");
  return S;
}
