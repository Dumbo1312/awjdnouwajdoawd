/*
 * ReadResult.exe
 *
 * Reads the UEFI variables written by HeciPspProbe.efi and prints a
 * human-readable test report to the console.
 *
 * Run as Administrator (needs SE_SYSTEM_ENVIRONMENT_NAME privilege).
 * Usage: ReadResult.exe
 */

/* ---- minimal Windows API declarations (no SDK headers needed) ----------- */
typedef unsigned char  BYTE;
typedef unsigned short WORD;
typedef unsigned int   DWORD;
typedef unsigned long long QWORD;
typedef void  *HANDLE;
typedef int    BOOL;
typedef char  *LPSTR;
typedef const char *LPCSTR;
typedef unsigned short WCHAR;
typedef const WCHAR   *LPCWSTR;
typedef void  *LPVOID;
typedef DWORD *LPDWORD;

#define NULL ((void*)0)
#define TRUE  1
#define FALSE 0

#define STD_OUTPUT_HANDLE ((DWORD)-11)

typedef struct { DWORD LowPart; DWORD HighPart; } LUID;
typedef struct { LUID Luid; DWORD Attributes; } LUID_AND_ATTRIBUTES;
typedef struct { DWORD PrivilegeCount; LUID_AND_ATTRIBUTES Privileges[1]; } TOKEN_PRIVILEGES;
#define TOKEN_ADJUST_PRIVILEGES 0x0020
#define TOKEN_QUERY             0x0008
#define SE_PRIVILEGE_ENABLED    0x00000002

__declspec(dllimport) HANDLE __stdcall GetStdHandle(DWORD);
__declspec(dllimport) BOOL   __stdcall WriteConsoleA(HANDLE,const void*,DWORD,LPDWORD,LPVOID);
__declspec(dllimport) DWORD  __stdcall GetFirmwareEnvironmentVariableW(LPCWSTR,LPCWSTR,LPVOID,DWORD);
__declspec(dllimport) DWORD  __stdcall GetLastError(void);
__declspec(dllimport) HANDLE __stdcall GetCurrentProcess(void);
__declspec(dllimport) BOOL   __stdcall OpenProcessToken(HANDLE,DWORD,HANDLE*);
__declspec(dllimport) BOOL   __stdcall LookupPrivilegeValueA(LPCSTR,LPCSTR,LUID*);
__declspec(dllimport) BOOL   __stdcall AdjustTokenPrivileges(HANDLE,BOOL,TOKEN_PRIVILEGES*,DWORD,LPVOID,LPDWORD);
__declspec(dllimport) void   __stdcall ExitProcess(DWORD);

/* ---- result structs (must match HeciPspCommon.h exactly) ---------------- */
#pragma pack(push,1)
typedef struct {
  DWORD Status;
  DWORD HCsr;
  DWORD MeCsr;
  WORD  FwMajor; WORD FwMinor;
  WORD  FwBuild; WORD FwHotfix;
} HECI_RESULT;

typedef struct {
  DWORD Status;
  DWORD SmnMbox;
  DWORD FeatReg;
  DWORD StsReg;
  DWORD CmdRetCode;
  BYTE  PspReady;
  BYTE  Recovery;
  BYTE  Pad[2];
} PSP_RESULT;

typedef struct {
  DWORD Status;
  DWORD Q0Ctrl;
  DWORD Q0Status;
  DWORD Q0IntSts;
  DWORD DescPa;
  DWORD Mismatch;
} CCP_RESULT;
#pragma pack(pop)

/* UEFI variable GUID: {DEADBEEF-1234-5678-AA-BB-CC-DD-EE-FF-00-11} */
static const WCHAR kGuid[] =
  L"{DEADBEEF-1234-5678-AABB-CCDDEEFF0011}";

/* ---- tiny string/print helpers ------------------------------------------ */
static HANDLE gOut;

static void WriteStr(const char *s) {
  DWORD n = 0;
  const char *p = s;
  while (*p++) n++;
  WriteConsoleA(gOut, s, n, &n, NULL);
}

static void WriteChar(char c) {
  DWORD n;
  WriteConsoleA(gOut, &c, 1, &n, NULL);
}

static void WriteHex32(DWORD v) {
  const char *H = "0123456789ABCDEF";
  int i;
  WriteStr("0x");
  for (i = 28; i >= 0; i -= 4) WriteChar(H[(v >> i) & 0xF]);
}

static void WriteDec(DWORD v) {
  char buf[12]; int i = 11;
  buf[i] = 0;
  if (!v) { WriteChar('0'); return; }
  while (v) { buf[--i] = '0' + (v % 10); v /= 10; }
  WriteStr(buf + i);
}

static void WriteNl(void)  { WriteStr("\r\n"); }
static void WriteOk(void)  { WriteStr("  [  OK  ]  "); }
static void WriteFail(void){ WriteStr("  [ FAIL ]  "); }
static void WriteWarn(void){ WriteStr("  [ WARN ]  "); }
static void WriteInfo(void){ WriteStr("  [ INFO ]  "); }

/* ---- privilege escalation ------------------------------------------------ */
static void EnableFirmwarePrivilege(void) {
  HANDLE tok;
  TOKEN_PRIVILEGES tp;
  if (!OpenProcessToken(GetCurrentProcess(),
        TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) return;
  LookupPrivilegeValueA(NULL, "SeSystemEnvironmentPrivilege", &tp.Privileges[0].Luid);
  tp.PrivilegeCount = 1;
  tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
  AdjustTokenPrivileges(tok, FALSE, &tp, 0, NULL, NULL);
}

/* ---- read one UEFI variable ---------------------------------------------- */
static DWORD ReadVar(const WCHAR *Name, void *Buf, DWORD BufSz) {
  DWORD n = GetFirmwareEnvironmentVariableW(Name, kGuid, Buf, BufSz);
  return n;
}

/* ---- HECI result display ------------------------------------------------- */
static const char *HeciStatusStr(DWORD s) {
  switch (s) {
  case 0: return "OK";
  case 1: return "ME_NOT_READY";
  case 2: return "RESET_FAILED";
  case 3: return "NOT_READY";
  case 4: return "SEND_FAILED";
  case 5: return "RECV_FAILED";
  case 7: return "NOT_FOUND";
  case 8: return "ALLOC_FAILED";
  case 9: return "CMD_TIMEOUT";
  default: return "UNKNOWN";
  }
}

static void PrintHeci(void) {
  HECI_RESULT r;
  DWORD n;

  WriteStr("========================================\r\n");
  WriteStr("  Intel ME / HECI\r\n");
  WriteStr("========================================\r\n");

  n = ReadVar(L"HECIResult", &r, sizeof(r));
  if (!n) {
    WriteFail(); WriteStr("HECIResult variable not found (not an Intel platform, or driver did not run)\r\n\r\n");
    return;
  }

  WriteStr("  Status    : "); WriteStr(HeciStatusStr(r.Status)); WriteNl();
  WriteStr("  H_CSR     : "); WriteHex32(r.HCsr);
  WriteStr("  (RDY="); WriteChar((r.HCsr >> 27) & 1 ? '1' : '0');
  WriteStr(" RST="); WriteChar((r.HCsr >> 28) & 1 ? '1' : '0');
  WriteStr(" CBD="); WriteDec(r.HCsr & 0xFF); WriteStr(")\r\n");
  WriteStr("  ME_CSR    : "); WriteHex32(r.MeCsr);
  WriteStr("  (RDY="); WriteChar((r.MeCsr >> 27) & 1 ? '1' : '0');
  WriteStr(")\r\n");

  if (r.Status == 0) {
    WriteOk();
    WriteStr("ME firmware version: ");
    WriteDec(r.FwMajor); WriteChar('.');
    WriteDec(r.FwMinor); WriteChar('.');
    WriteDec(r.FwBuild); WriteChar('.');
    WriteDec(r.FwHotfix); WriteNl();
  } else {
    WriteFail(); WriteStr("HECI probe failed: "); WriteStr(HeciStatusStr(r.Status)); WriteNl();
  }
  WriteNl();
}

/* ---- PSP result display -------------------------------------------------- */
static const char *PspStatusStr(DWORD s) {
  switch (s) {
  case 0: return "OK";
  case 3: return "NOT_READY";
  case 6: return "SMN_OK (BAR2 not found)";
  case 7: return "NOT_FOUND";
  case 8: return "ALLOC_FAILED";
  case 9: return "CMD_TIMEOUT";
  default: return "UNKNOWN";
  }
}

static void PrintPsp(void) {
  PSP_RESULT r;
  DWORD n;

  WriteStr("========================================\r\n");
  WriteStr("  AMD PSP\r\n");
  WriteStr("========================================\r\n");

  n = ReadVar(L"PSPResult", &r, sizeof(r));
  if (!n) {
    WriteFail(); WriteStr("PSPResult variable not found (not an AMD platform, or driver did not run)\r\n\r\n");
    return;
  }

  WriteStr("  Status    : "); WriteStr(PspStatusStr(r.Status)); WriteNl();
  WriteStr("  SMN Mbox  : "); WriteHex32(r.SmnMbox); WriteNl();
  WriteStr("  FeatReg   : "); WriteHex32(r.FeatReg); WriteNl();
  WriteStr("  StsReg    : "); WriteHex32(r.StsReg);
  WriteStr("  (DONE="); WriteChar((r.StsReg >> 31) & 1 ? '1' : '0');
  WriteStr(" ERR="); WriteChar((r.StsReg >> 1) & 1 ? '1' : '0'); WriteStr(")\r\n");
  WriteStr("  CmdRet    : "); WriteHex32(r.CmdRetCode); WriteNl();
  WriteStr("  PSP READY : "); WriteChar(r.PspReady ? '1' : '0'); WriteNl();
  WriteStr("  Recovery  : "); WriteChar(r.Recovery  ? '1' : '0'); WriteNl();

  if (r.Status == 0) {
    WriteOk(); WriteStr("PSP mailbox command accepted\r\n");
  } else if (r.Status == 6) {
    WriteWarn(); WriteStr("SMN readable but BAR2 not mapped — PSP found via SMN only\r\n");
  } else {
    WriteFail(); WriteStr("PSP probe issue: "); WriteStr(PspStatusStr(r.Status)); WriteNl();
  }
  WriteNl();
}

/* ---- CCP result display -------------------------------------------------- */
static const char *CcpStatusStr(DWORD s) {
  switch (s) {
  case 0: return "OK";
  case 1: return "ALLOC_FAIL";
  case 2: return "TIMEOUT";
  case 3: return "HW_ERROR";
  case 4: return "VERIFY_FAIL";
  case 5: return "NO_DEVICE";
  default: return "UNKNOWN";
  }
}

static void PrintCcp(void) {
  CCP_RESULT r;
  DWORD n;

  WriteStr("========================================\r\n");
  WriteStr("  AMD CCP DMA engine (async memory R/W)\r\n");
  WriteStr("========================================\r\n");

  n = ReadVar(L"CCPResult", &r, sizeof(r));
  if (!n) {
    WriteFail(); WriteStr("CCPResult variable not found (AMD PSP/CCP not found, or driver did not run)\r\n\r\n");
    return;
  }

  WriteStr("  Status    : "); WriteStr(CcpStatusStr(r.Status)); WriteNl();
  WriteStr("  Q0_CTRL   : "); WriteHex32(r.Q0Ctrl);
  WriteStr("  (enabled="); WriteChar(r.Q0Ctrl & 1 ? '1' : '0'); WriteStr(")\r\n");
  WriteStr("  Q0_STATUS : "); WriteHex32(r.Q0Status);
  WriteStr("  (error="); WriteChar((r.Q0Status >> 1) & 1 ? '1' : '0'); WriteStr(")\r\n");
  WriteStr("  Q0_INT_STS: "); WriteHex32(r.Q0IntSts);
  WriteStr("  (IOC="); WriteChar(r.Q0IntSts & 1 ? '1' : '0'); WriteStr(")\r\n");
  WriteStr("  Desc PA   : "); WriteHex32(r.DescPa); WriteNl();

  if (r.Status == 0) {
    WriteOk();
    WriteStr("CCP DMA 4096-byte passthrough copy verified  <<< async memory engine WORKS\r\n");
    WriteStr("         x86 cores were NOT frozen during this copy (no SMI issued)\r\n");
  } else if (r.Status == 2) {
    WriteFail(); WriteStr("CCP queue timeout — Q0_INT_STS IOC bit never set\r\n");
    WriteStr("         Possible causes:\r\n");
    WriteStr("           - Register offsets wrong for this board (check serial log)\r\n");
    WriteStr("           - PSP firmware locked the CCP queue\r\n");
    WriteStr("           - BAR2 not fully mapped (some Zen 1 boards)\r\n");
    if (r.Q0Ctrl == 0xFFFFFFFF)
      WriteStr("         Q0_CTRL reads all-FF -> BAR2 not accessible at CCP offset\r\n");
  } else if (r.Status == 3) {
    WriteFail(); WriteStr("CCP engine error (Q0_STATUS error bit set)\r\n");
  } else if (r.Status == 4) {
    WriteFail();
    WriteStr("CCP copy ran but data mismatch at DWORD index "); WriteDec(r.Mismatch);
    WriteStr(" — descriptor submitted but data not coherent?\r\n");
  } else {
    WriteFail(); WriteStr("CCP status: "); WriteStr(CcpStatusStr(r.Status)); WriteNl();
  }
  WriteNl();
}

/* ---- entry point --------------------------------------------------------- */
void __stdcall mainCRTStartup(void) {
  DWORD err;

  gOut = GetStdHandle(STD_OUTPUT_HANDLE);

  WriteStr("\r\n");
  WriteStr("  HeciPspProbe — result reader\r\n");
  WriteStr("  ==============================\r\n\r\n");

  EnableFirmwarePrivilege();

  /* Verify we can actually read firmware variables */
  {
    BYTE dummy[4];
    GetFirmwareEnvironmentVariableW(L"", L"{00000000-0000-0000-0000-000000000000}",
                                    dummy, sizeof(dummy));
    err = GetLastError();
    if (err == 1314) { /* ERROR_PRIVILEGE_NOT_HELD */
      WriteFail();
      WriteStr("ERROR: Need to run as Administrator\r\n");
      WriteStr("       Right-click cmd.exe -> Run as administrator, then run again.\r\n\r\n");
      ExitProcess(1);
    }
  }

  PrintHeci();
  PrintPsp();
  PrintCcp();

  WriteStr("========================================\r\n");
  WriteStr("  Done. Press any key or close window.\r\n");
  WriteStr("========================================\r\n\r\n");

  ExitProcess(0);
}
