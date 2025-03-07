/*********************************************************************
*                     SEGGER Microcontroller GmbH                    *
*                        The Embedded Experts                        *
**********************************************************************
*                                                                    *
*       (c) 2003 - 2022  SEGGER Microcontroller GmbH                 *
*                                                                    *
*       www.segger.com     Support: support_emfile@segger.com        *
*                                                                    *
**********************************************************************
*                                                                    *
*       emFile * File system for embedded applications               *
*                                                                    *
*                                                                    *
*       Please note:                                                 *
*                                                                    *
*       Knowledge of this file may under no circumstances            *
*       be used to write a similar product for in-house use.         *
*                                                                    *
*       Thank you for your fairness !                                *
*                                                                    *
**********************************************************************
*                                                                    *
*       emFile version: V5.20.0                                      *
*                                                                    *
**********************************************************************
----------------------------------------------------------------------
Licensing information
Licensor:                 SEGGER Microcontroller Systems LLC
Licensed to:              React Health Inc, 203 Avenue A NW, Suite 300, Winter Haven FL 33881, USA
Licensed SEGGER software: emFile
License number:           FS-00855
License model:            SSL [Single Developer Single Platform Source Code License]
Licensed product:         -
Licensed platform:        STM32F4, IAR
Licensed number of seats: 1
----------------------------------------------------------------------
Support and Update Agreement (SUA)
SUA period:               2022-05-19 - 2022-11-19
Contact to extend SUA:    sales@segger.com
----------------------------------------------------------------------
File        : FS_WinDrive.c
Purpose     : Device Driver using Windows I/O functions for
              logical sector access.
-------------------------- END-OF-HEADER -----------------------------
*/

#ifdef _WIN32

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "FS_Int.h"

#define WIN32_LEAN_AND_MEAN
#include "FS_SIM_GUI_WIN32_Res.h"
#include <commctrl.h>
#include <commdlg.h>
#include <winioctl.h>
#include <stdio.h>

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/

/*********************************************************************
*
*       ASSERT_UNIT_NO_IS_IN_RANGE
*/
#if FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_CHECK_ALL
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)                             \
  if (Unit >= FS_WINDRIVE_NUM_UNITS) {                                 \
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "WIN: Invalid unit number.")); \
    FS_X_PANIC(FS_ERRCODE_INVALID_PARA);                               \
  }
#else
  #define ASSERT_UNIT_NO_IS_IN_RANGE(Unit)
#endif

#define WIN_SIZE_X              (320)
#define WIN_SIZE_Y              (200)
#define WIN_MIN_SIZE_X          (WIN_SIZE_X)
#define WIN_MIN_SIZE_Y          (WIN_SIZE_Y)
#define WIN_DIST_XY             5

#define ID_CB_DRIVE             ID_COMBO0
#define ID_ED_FILE              ID_EDIT0
#define ID_DRIVE0               300
#define ID_BTN_SEL_FILE         ID_BUTTON0
#define ID_BTN_CREATE_IMG       ID_BUTTON1

#define ID_ED_NUMSECTORS        ID_EDIT1
#define ID_ED_SECTORSIZE        ID_EDIT2

#define REG_PATH                "Software\\Segger\\FS\\Windrive"

/*********************************************************************
*
*       Local data types
*
**********************************************************************
*/
typedef struct {
  HANDLE  hStorage;             // Handle to the opened Windows drive or image file.
  U32     BytesPerSector;       // Number of bytes in a logical sector configured by the application.
  U8      IsDrive;              // Set to 1 if a Windows drive is used as storage.
  U8      IsInteractive;        // Set to 1 if the user is asked to select a drive or an image file.
  U8      SuppressErrors;       // Set to 1 if no errors have to be reported.
  U8      SuppressWarnings;     // Set to 1 if no warnings have to be reported.
  U32     NumSectors;           // Number of sectors configured by the application.
  WCHAR   acName[FS_MAX_PATH];  // Path to the Windows drive or image file used as storage.
  U32     DataBufferAlignment;  // Alignment of the data passed to the Windows API functions as a power of 2 value.
  void  * pDataBuffer;          // Aligned buffer used for passing the data to Windows API functions.
  U32     SizeOfDataBuffer;     // Number of bytes allocated for pDataBuffer.
  U8      IsLocked;             // Set to 1 if the driver has exclusive access to a Windows drive.
} WINDRIVE_INST;

/*********************************************************************
*
*       Static data
*
**********************************************************************
*/
static WINDRIVE_INST * _apInst[FS_WINDRIVE_NUM_UNITS];   // List of driver instances.
static U8              _NumUnits = 0;                       // Number of driver instances.
static HINSTANCE       _hDialog;                            // Instance of the dialog box that allows the selection of a Windows drive.
static HWND            _hWndMain;                           // Instance of the main window of the application.
static RECT            _rPrev;                              // Contains the previous rectangle of main windows client area. (Used for resizing of dialog items)
static WCHAR           _acFileName[MAX_PATH];
static U8              _UnitToConfig;

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/

/*********************************************************************
*
*       _GetInst
*
*  Function description
*    Returns a driver instance by unit number.
*/
static WINDRIVE_INST * _GetInst(U8 Unit) {
  WINDRIVE_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_WINDRIVE_NUM_UNITS) {
    pInst = _apInst[Unit];
  }
  return pInst;
}

/*********************************************************************
*
*       _AllocDataBuffer
*
*  Function description
*    Allocates a data buffer aligned to the sector size.
*/
static void * _AllocDataBuffer(WINDRIVE_INST * pInst, U32 NumBytes) {
  void * pBuffer;
  U32    SizeOfBuffer;
  U32    Alignment;

  pBuffer      = pInst->pDataBuffer;
  SizeOfBuffer = pInst->SizeOfDataBuffer;
  Alignment    = pInst->DataBufferAlignment;
  //
  // Free the old buffer if a larger one is required.
  //
  if (pBuffer != NULL) {
    if (NumBytes > SizeOfBuffer) {
      _aligned_free(pBuffer);
      pBuffer      = NULL;
      SizeOfBuffer = 0;
    }
  }
  //
  // Allocate a new buffer if required.
  //
  if (pBuffer == NULL) {
    pBuffer = _aligned_malloc(NumBytes, Alignment);
    if (pBuffer != NULL) {
      SizeOfBuffer = NumBytes;
    }
  }
  pInst->pDataBuffer      = pBuffer;
  pInst->SizeOfDataBuffer = SizeOfBuffer;
  return pBuffer;
}

/*********************************************************************
*
*       _FreeDataBuffer
*
*  Function description
*    Frees the internal data buffer.
*/
static void _FreeDataBuffer(WINDRIVE_INST * pInst) {
  void * pBuffer;
  U32    SizeOfBuffer;

  pBuffer      = pInst->pDataBuffer;
  SizeOfBuffer = pInst->SizeOfDataBuffer;
  if (pBuffer != NULL) {
    _aligned_free(pBuffer);
    pBuffer      = NULL;
    SizeOfBuffer = 0;
  }
  pInst->pDataBuffer      = pBuffer;
  pInst->SizeOfDataBuffer = SizeOfBuffer;
}

/*********************************************************************
*
*       _ShowError
*
*  Function description
*    Displays a message box that indicates an error.
*
*  Parameters
*    pInst        Driver instance.
*    sMessage     The message to be displayed.
*    ErrCode      Windows error code if available.
*/
static void _ShowError(const WINDRIVE_INST * pInst, LPCWSTR sMessage, DWORD ErrCode) {
  WCHAR    ac[512];
  LPVOID   pMessageBuffer;
  int      IsInteractive;
  int      SuppressErrors;
  WCHAR  * s;

  IsInteractive  = 0;
  SuppressErrors = 0;
  if (pInst != NULL) {
    IsInteractive  = pInst->IsInteractive;
    SuppressErrors = pInst->SuppressErrors;
  }
  if (SuppressErrors == 0) {
    if (ErrCode == 0) {
      if (IsInteractive != 0) {
        MessageBoxW(NULL, sMessage, L"WinDrive Error", MB_OK | MB_ICONWARNING);
      }
    } else {
      //
      // Make it human readable and format the error message
      //
      FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                     NULL, ErrCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&pMessageBuffer, 0, NULL);
      //
      // Remove the trailing new line character.
      //
      s = wcsrchr(pMessageBuffer, '\r');
      if (s != NULL) {
        *s = '\0';
      }
      FS_MEMSET(ac, 0, sizeof(ac));
      _snwprintf(ac, SEGGER_COUNTOF(ac) - 1, L"%s (Code: 0x%.8x, Desc: %s)", sMessage, ErrCode, (LPWSTR)pMessageBuffer);
      ac[SEGGER_COUNTOF(ac) - 1] = '\0';
      //
      // Free the buffer, the buffer is automatically allocated by the FormatMessage.
      // The application has to free the buffer by using LocalFree
      //
      LocalFree(pMessageBuffer);
      //
      // Display the error message.
      //
      if (IsInteractive != 0) {
        MessageBoxW(NULL, ac, L"WinDrive Error", MB_OK | MB_ICONWARNING);
      }
      sMessage = ac;
    }
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS)
    {
      char acDebug[1024];
      int  NumBytes;

      NumBytes = WideCharToMultiByte(CP_ACP, 0, sMessage, -1, acDebug, sizeof(acDebug), NULL, NULL);
      if (NumBytes != 0) {
        FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "WIN: %s", acDebug));
      }
    }
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS
  }
}

/*********************************************************************
*
*       _ShowWarning
*
*  Function description
*    Displays a message box that indicates a warning.
*
*  Parameters
*    pInst        Driver instance.
*    sMessage     The message to be displayed.
*/
static void _ShowWarning(const WINDRIVE_INST * pInst, LPCWSTR sMessage) {
  int IsInteractive;
  int SuppressWarnings;

  IsInteractive    = 0;
  SuppressWarnings = 0;
  if (pInst != NULL) {
    IsInteractive    = pInst->IsInteractive;
    SuppressWarnings = pInst->SuppressWarnings;
  }
  if (SuppressWarnings == 0) {
    if (IsInteractive != 0) {
      MessageBoxW(NULL, sMessage, L"WinDrive Warning", MB_OK | MB_ICONWARNING);
    }
#if (FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_WARNINGS)
    {
      char acDebug[1024];
      int  NumBytes;

      NumBytes = WideCharToMultiByte(CP_ACP, 0, sMessage, -1, acDebug, sizeof(acDebug), NULL, NULL);
      if (NumBytes != 0) {
        FS_DEBUG_WARN((FS_MTYPE_DRIVER, "WIN: %s", acDebug));
      }
    }
#endif // FS_DEBUG_LEVEL >= FS_DEBUG_LEVEL_LOG_ERRORS
  }
}

/*********************************************************************
*
*       _GetInitialWinRect
*/
static void _GetInitialWinRect(RECT * pRect, LONG Width, LONG Height) {
  RECT rParent, rDesk;
  int  x, y;

  pRect->left   = 0;
  pRect->top    = 0;
  pRect->right  = Width;
  pRect->bottom = Height;
  GetWindowRect(_hWndMain, &rParent);
  SystemParametersInfo(SPI_GETWORKAREA, 0, &rDesk, 0);
  x = rParent.left + ((rParent.right  - rParent.left) - Width) / 2;
  y = rParent.top  + ((rParent.bottom - rParent.top)  - Height) / 2;
  x = SEGGER_MAX(SEGGER_MIN(x, rDesk.right  - (int)Width),  0);
  y = SEGGER_MAX(SEGGER_MIN(y, rDesk.bottom - (int)Height), 0);
  OffsetRect(pRect, x, y);
}

/*********************************************************************
*
*       _SetDefaultFont
*/
static void _SetDefaultFont(HWND hWnd) {
  HFONT  hfnt = (HFONT)GetStockObject(ANSI_VAR_FONT);
  SendMessage(hWnd, WM_SETFONT, (WPARAM) hfnt, MAKELPARAM(1, 0));
}

/*********************************************************************
*
*       _AddDlgItemEx
*/
static HWND _AddDlgItemEx(HWND hDlg, LPCWSTR sClass, LPCWSTR sName, int x, int y, int w, int h, int Id, DWORD Flags, DWORD ExFlags)
{
  HWND hWin;

  hWin = CreateWindowExW(ExFlags, sClass, sName, Flags, x, y, w, h, hDlg, NULL, _hDialog, NULL);
  _SetDefaultFont(hWin);
#if (_MSC_VER <= 1200)
  SetWindowLong(hWin, GWL_ID, Id);
#else
  SetWindowLongPtr(hWin, GWLP_ID, Id);
#endif
  return hWin;
}

/*********************************************************************
*
*       _AddDlgItem
*/
static HWND _AddDlgItem(HWND hDlg, LPCWSTR sClass, LPCWSTR sName, int x, int y, int w, int h, int Id, DWORD Flags, DWORD ExFlags)
{
  Flags |= WS_CLIPCHILDREN | WS_CHILD | WS_VISIBLE;
  return _AddDlgItemEx(hDlg, sClass, sName, x, y, w, h, Id, Flags, ExFlags);
}

/*********************************************************************
*
*       _ComboboxAddString
*/
static void _ComboboxAddString(HWND hCombo, const char * pText, int Id) {
  LRESULT NumItems = SendMessage(hCombo, CB_GETCOUNT, 0, 0);
  SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM) (LPCTSTR)pText);
  SendMessage(hCombo, CB_SETITEMDATA ,(WPARAM)NumItems, (LPARAM) (DWORD) Id);
}

/*********************************************************************
*
*       _OnNewFile
*/
static void _OnNewFile(HWND hWnd) {
  OPENFILENAME Ofn = {0};
  char         acFileName[MAX_PATH];

  acFileName[0]         = 0;
  Ofn.lStructSize       = sizeof(Ofn);
  Ofn.hwndOwner         = hWnd;
  Ofn.hInstance         = _hDialog;
  Ofn.lpstrFilter       = "Image Files (*.img, *.bin, *.raw)\0*.img;*.bin;*.raw\0\0";
  Ofn.lpstrCustomFilter = NULL;
  Ofn.nMaxCustFilter    = 0;
  Ofn.nFilterIndex      = 0;
  Ofn.lpstrFile         = &acFileName[0];
  Ofn.nMaxFile          = sizeof(acFileName);
  Ofn.lpstrFileTitle    = NULL;
  Ofn.nMaxFileTitle     = 0;
  Ofn.lpstrInitialDir   = NULL;
  Ofn.lpstrTitle        = 0;
  Ofn.Flags             = OFN_CREATEPROMPT | OFN_PATHMUSTEXIST;
  Ofn.nFileOffset       = 0;
  Ofn.nFileExtension    = 0;
  Ofn.lpstrDefExt       = "img";
  Ofn.lCustData         = 0;
  Ofn.lpfnHook          = NULL;
  Ofn.lpTemplateName    = NULL;
  GetOpenFileName(&Ofn);
  SetDlgItemText(hWnd, ID_ED_FILE, acFileName);
}

/*********************************************************************
*
*       _CreateImageFile
*/
static int _CreateImageFile(const WINDRIVE_INST * pInst, LPCWSTR sFileName, unsigned NumSectors, unsigned SectorSize) {
  HANDLE   hFile;
  U32      NumBytes;
  void   * p;
  U32      NumBytesWritten;
  WCHAR    ac[256];
  BOOL     Result;
  int      r;

  hFile = CreateFileW(sFileName, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
  if (hFile == INVALID_HANDLE_VALUE) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "WIN: _CreateImageFile: Could not create image file (Open failure)"));
    _snwprintf(ac, SEGGER_COUNTOF(ac), L"Could not create \"%s\" image file", sFileName);
    _ShowError(pInst, ac, GetLastError());
    return 1;
  }
  NumBytes = NumSectors * SectorSize;
  p = malloc(NumBytes);
  if (p == NULL) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "WIN: _CreateImageFile: Could not create image file (Not enough memory)"));
    _snwprintf(ac, SEGGER_COUNTOF(ac), L"Could not create \"%s\" image file (Not enough memory)", sFileName);
    _ShowError(pInst, ac, 0);
    return 1;
  }
  r = 0;
  FS_MEMSET(p, 0, NumBytes);
  NumBytesWritten = 0;
  Result = WriteFile(hFile, p, NumBytes, &NumBytesWritten, NULL);
  if ((Result == FALSE) || (NumBytesWritten != NumBytes)) {
    FS_DEBUG_ERROROUT((FS_MTYPE_DRIVER, "WIN: _CreateImageFile: Could not create image file (Write failure)"));
    _snwprintf(ac, SEGGER_COUNTOF(ac), L"Could not create \"%s\" image file", sFileName);
    _ShowError(pInst, ac, GetLastError());
    r = 1;
  }
  CloseHandle(hFile);
  free(p);
  return r;
}

/*********************************************************************
*
*       _OnInitCreateImageDialog
*/
static BOOL _OnInitCreateImageDialog(HWND hWnd) {
  int      x, y;
  RECT     r = {0};
  HICON    hIcon;
#if (_MSC_VER <= 1200)
  LONG     Style;
#else
  LONG_PTR Style;
#endif

  //
  // Initialize the dialog window.
  //
  _GetInitialWinRect(&r, 300, 200);
  SetWindowPos (hWnd, 0, r.left, r.top, (r.right - r.left), (r.bottom - r.top), SWP_NOZORDER);
  SetWindowText(hWnd, "Create image file");
#if (_MSC_VER <= 1200)
  Style = GetWindowLong(hWnd, GWL_STYLE);
  Style |= (LONG)(DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU);
  SetWindowLong(hWnd, GWL_STYLE, Style);
#else
  Style = GetWindowLongPtr(hWnd, GWL_STYLE);
  Style |= DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU;
  SetWindowLongPtr(hWnd, GWL_STYLE, Style);
#endif
  //
  // Add icon to dialog box
  //
  hIcon = (HICON)LoadImage(_hDialog, MAKEINTRESOURCE(IDI_ICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
  SendMessage(hWnd, WM_SETICON, (WPARAM)ICON_BIG, (LPARAM)hIcon);
  x = 0;
  y = 0;
  _AddDlgItem(hWnd, L"STATIC",   L"Image file name",               15+x,   3+y, 105,  15, IDC_STATIC,        0                                           , 0);
  _AddDlgItem(hWnd, L"EDIT",     NULL,                             15+x,  18+y, 210,  23, ID_ED_FILE,        WS_TABSTOP  | ES_AUTOHSCROLL                , WS_EX_CLIENTEDGE);
  _AddDlgItem(hWnd, L"BUTTON",   L"...",                          225+x,  18+y,  26,  23, ID_BTN_SEL_FILE,   WS_TABSTOP                                  , 0);
  _AddDlgItem(hWnd, L"STATIC",   L"Number of sectors",             15+x,  50+y, 105,  15, IDC_STATIC,        0                                           , 0);
  _AddDlgItem(hWnd, L"EDIT",     NULL,                             15+x,  65+y, 105,  23, ID_ED_NUMSECTORS,  WS_TABSTOP  | ES_AUTOHSCROLL   |  ES_NUMBER , WS_EX_CLIENTEDGE);
  _AddDlgItem(hWnd, L"STATIC",   L"Sectors size",                 184+x,  50+y,  70,  15, IDC_STATIC,        0                                           , 0);
  _AddDlgItem(hWnd, L"EDIT",     NULL,                            184+x,  65+y,  70,  23, ID_ED_SECTORSIZE,  WS_TABSTOP  | ES_AUTOHSCROLL   |  ES_NUMBER | ES_READONLY, WS_EX_CLIENTEDGE);
  _AddDlgItem(hWnd, L"BUTTON",   L"&Create",                       15+x, 103+y,  60,  23, IDOK ,             WS_TABSTOP  | BS_DEFPUSHBUTTON              , 0);
  _AddDlgItem(hWnd, L"BUTTON",   L"C&ancel",                      195+x, 103+y,  60,  23, IDCANCEL,          WS_TABSTOP  | BS_PUSHBUTTON                 , 0);
  _AddDlgItem(hWnd, L"STATIC",   L"x",                            152+x,  68+y,  12,  16, IDC_STATIC,        0                                           , 0);
  SetDlgItemText(hWnd, ID_ED_SECTORSIZE, "512");
  SetFocus(GetDlgItem(hWnd, IDOK));
  return FALSE;  // We have initially set the focus, when we return FALSE.
}

/*********************************************************************
*
*       _cbCreateImageDialog
*/
static BOOL CALLBACK _cbCreateImageDialog(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
  int ItemId = LOWORD(wParam);
  int r      = 0;

  FS_USE_PARA(lParam);
  switch (Msg) {
  case WM_INITDIALOG:
    return _OnInitCreateImageDialog(hWnd);
  case WM_CLOSE:
    EndDialog(hWnd, 0);
    return FALSE;
  case WM_COMMAND:
    switch (ItemId) {
    case ID_BTN_SEL_FILE:
      _OnNewFile(hWnd);
      break;
    case IDOK:
      {
        char            acBuffer[32];
        WCHAR           acFileName[MAX_PATH];
        unsigned        NumSectors;
        unsigned        SectorSize;
        WINDRIVE_INST * pInst;

        pInst = _GetInst(_UnitToConfig);
        GetDlgItemText(hWnd, ID_ED_NUMSECTORS, &acBuffer[0], sizeof(acBuffer));
        NumSectors = (unsigned)atoi(acBuffer);
        if (NumSectors == 0) {
          _ShowError(pInst, L"Wrong number of sectors entered", 0);
          SetDlgItemText(hWnd, ID_ED_NUMSECTORS, "0");
          break;
        }
        GetDlgItemText(hWnd, ID_ED_SECTORSIZE, &acBuffer[0], sizeof(acBuffer));
        SectorSize = (unsigned)atoi(acBuffer);
        if (SectorSize != 512) {
          _ShowError(pInst, L"Sector size must be 512 bytes", 0);
          SetDlgItemText(hWnd, ID_ED_SECTORSIZE, "512");
          break;
        }
        FS_MEMSET(acFileName, 0, sizeof(acFileName));
        GetDlgItemTextW(hWnd, ID_ED_FILE, acFileName, sizeof(acFileName) / 2);
        if (acFileName[0] == '\0') {
          _ShowError(pInst, L"Image file name is missing", 0);
          _OnNewFile(hWnd);
          break;
        }
        r = 1;
        _CreateImageFile(pInst, acFileName, NumSectors, SectorSize);
        (void)wcsncpy(_acFileName, acFileName, SEGGER_COUNTOF(_acFileName) - 1);
        _acFileName[SEGGER_COUNTOF(_acFileName) - 1] = '\0';
        EndDialog(hWnd, r);
        return FALSE;
      }
    case IDCANCEL:
      EndDialog(hWnd, r);
      return FALSE;
    default:
      break;
    }
    break;
  default:
    break;
  }
  return FALSE;
}

/*********************************************************************
*
*       _InitDriveCombo
*/
static void _InitDriveCombo(HWND hWnd) {
  HWND   hComboBox;
  char   acDir[MAX_PATH];
  char   acRootDrive[MAX_PATH];
  char * p;
  int    i;
  int    Id;
  U32    DriveMask;

  hComboBox = GetDlgItem(hWnd, ID_COMBO0);
  DriveMask = GetLogicalDrives();
  //
  //  Get the drive where windows is installed.
  //  This drive shall not be in the list.
  //
  (void)GetWindowsDirectory(&acDir[0], sizeof(acDir));
  p = FS_STRCHR(&acDir[0], '\\');
  if (p) {
    *p = 0;
  }
  SEGGER_snprintf(acRootDrive, sizeof(acRootDrive), "\\\\.\\%s", acDir);
  Id = 0;
  //
  // Check and add all available drives
  //
  for (i = 0; i < 26; i++) {
    char ac[20];

    SEGGER_snprintf(ac, sizeof(ac), "\\\\.\\%c:", i + 'A');
    if ((DriveMask & (1u << i)) != 0u) {
      unsigned DriveType;

      SEGGER_snprintf(acDir, sizeof(acDir), "%s\\", ac);
      DriveType = GetDriveType(acDir);
      if ((DriveType == DRIVE_REMOVABLE) ||
          (DriveType == DRIVE_RAMDISK)   ||
          (DriveType == DRIVE_FIXED))       {
        if (FS_STRCMP(acRootDrive, ac)) {
          _ComboboxAddString(hComboBox, ac, ID_DRIVE0 + Id++);
        }
      }
    }
  }
  SendMessage(hComboBox, CB_SETCURSEL, 0, 0);
}

/*********************************************************************
*
*       _UpdateDialog
*/
static void _UpdateDialog(HWND hWnd) {
  if (IsDlgButtonChecked(hWnd, ID_RADIO0) == BST_CHECKED) {
    CheckDlgButton(hWnd, ID_RADIO1, BST_UNCHECKED);
    EnableWindow(GetDlgItem(hWnd, ID_CB_DRIVE), 1);
    EnableWindow(GetDlgItem(hWnd, ID_ED_FILE) , 0);
    EnableWindow(GetDlgItem(hWnd, ID_BTN_SEL_FILE) , 0);
  } else if (IsDlgButtonChecked(hWnd, ID_RADIO1) == BST_CHECKED) {
    CheckDlgButton(hWnd, ID_RADIO0, BST_UNCHECKED);
    EnableWindow(GetDlgItem(hWnd, ID_CB_DRIVE)    , 0);
    EnableWindow(GetDlgItem(hWnd, ID_ED_FILE)     , 1);
    EnableWindow(GetDlgItem(hWnd, ID_BTN_SEL_FILE), 1);
  }
}

/*********************************************************************
*
*       _OnCreateImage
*/
static void _OnCreateImage(HWND hWnd) {
  if (DialogBox(_hDialog, MAKEINTRESOURCE(IDD_MAINDIALOG), hWnd, (DLGPROC)_cbCreateImageDialog)) {
    SetDlgItemTextW(hWnd, ID_ED_FILE, _acFileName);
    CheckDlgButton(hWnd, ID_RADIO1, BST_CHECKED);
  } else {
    CheckDlgButton(hWnd, ID_RADIO0, BST_CHECKED);
  }
  _UpdateDialog(hWnd);
}

/*********************************************************************
*
*       _OnInitChangeDialog
*/
static BOOL _OnInitChangeDialog(HWND hWnd) {
  int           x;
  int           y;
  RECT          r = {0};
  HICON         hIcon;
  const WCHAR * sFileName;

  sFileName = NULL;
  //
  // Check if there is a file name available.
  //
  if ((_acFileName[0] != '\\') && (_acFileName[0] != '\0')) {
    sFileName = _acFileName;
  }
  //
  // Initialize the dialog window.
  //
  _GetInitialWinRect(&r, WIN_SIZE_X, WIN_SIZE_Y);
  SetWindowPos (hWnd, 0, r.left, r.top, (r.right - r.left), (r.bottom - r.top), SWP_NOZORDER);
  GetClientRect(hWnd, &_rPrev);
  SetWindowText(hWnd, "WinDrive configuration");
  //
  // Add icon to dialog box
  //
  hIcon = (HICON)LoadImage(_hDialog, MAKEINTRESOURCE(IDI_ICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
  SendMessage(hWnd, WM_SETICON, (WPARAM)ICON_BIG, (LPARAM)hIcon);
  //
  // Create separator
  //
  x =  2; y = 50;
  _AddDlgItem(hWnd, L"STATIC",   NULL,                               1+x,   0+y, 443,   2,  0,                SS_BLACKFRAME      | SS_SUNKEN, 0);
  //
  // Create dialog items
  //
  x = -3; y = 0;
  _AddDlgItem(hWnd, L"BUTTON",   L"&Drive",                         11+x,  16+y,  55,  15, ID_RADIO0,         BS_AUTORADIOBUTTON | WS_TABSTOP | WS_GROUP  , 0);
  _AddDlgItem(hWnd, L"BUTTON",   L"&File",                          11+x,  65+y,  55,  15, ID_RADIO1,         BS_AUTORADIOBUTTON | WS_TABSTOP | 0         , 0);
  _AddDlgItem(hWnd, L"COMBOBOX", NULL,                              80+x,  16+y, 220, 120, ID_CB_DRIVE,       CBS_DROPDOWNLIST   | WS_TABSTOP | WS_VSCROLL, WS_EX_CLIENTEDGE);
  _AddDlgItem(hWnd, L"EDIT",     sFileName,                         80+x,  65+y, 200,  20, ID_ED_FILE,        WS_TABSTOP         | ES_AUTOHSCROLL         , WS_EX_CLIENTEDGE);
  _AddDlgItem(hWnd, L"BUTTON",   L"...",                           279+x,  66+y,  18,  18, ID_BTN_SEL_FILE,   WS_TABSTOP                                  , 0);
  _AddDlgItem(hWnd, L"BUTTON",   L"C&reate Image",                 165+x,  95+y,  80,  23, ID_BTN_CREATE_IMG, WS_TABSTOP                                  , 0);
  _AddDlgItem(hWnd, L"BUTTON",   L"&OK",                           165+x, 125+y,  60,  23, IDOK ,             WS_TABSTOP         | BS_DEFPUSHBUTTON       , 0);
  _AddDlgItem(hWnd, L"BUTTON",   L"&Cancel",                       235+x, 125+y,  60,  23, IDCANCEL,          WS_TABSTOP         | BS_PUSHBUTTON          , 0);
  _InitDriveCombo(hWnd);
  if (sFileName) {
    CheckDlgButton(hWnd, ID_RADIO1, BST_CHECKED);
  } else {
    CheckDlgButton(hWnd, ID_RADIO0, BST_CHECKED);
  }
  _UpdateDialog(hWnd);
  SetFocus(GetDlgItem(hWnd, IDOK));
  return FALSE;  // We have initially set the focus, when we return FALSE.
}

/*********************************************************************
*
*       _OnSelectFile
*/
static void _OnSelectFile(HWND hWnd) {
  OPENFILENAME Ofn = {0};
  char         acFileName[MAX_PATH];

  acFileName[0]         = 0;
  Ofn.lStructSize       = sizeof(Ofn);
  Ofn.hwndOwner         = hWnd;
  Ofn.hInstance         = _hDialog;
  Ofn.lpstrFilter       = "Image Files (*.img, *.bin, *.raw)\0*.img;*.bin;*.raw\0\0";
  Ofn.lpstrCustomFilter = NULL;
  Ofn.nMaxCustFilter    = 0;
  Ofn.nFilterIndex      = 0;
  Ofn.lpstrFile         = &acFileName[0];
  Ofn.nMaxFile          = sizeof(acFileName);
  Ofn.lpstrFileTitle    = NULL;
  Ofn.nMaxFileTitle     = 0;
  Ofn.lpstrInitialDir   = NULL;
  Ofn.lpstrTitle        = 0;
  Ofn.Flags             = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  Ofn.nFileOffset       = 0;
  Ofn.nFileExtension    = 0;
  Ofn.lpstrDefExt       = NULL;
  Ofn.lCustData         = 0;
  Ofn.lpfnHook          = NULL;
  Ofn.lpTemplateName    = NULL;
  GetOpenFileName(&Ofn);
  SetDlgItemText(hWnd, ID_ED_FILE, acFileName);
}

/*********************************************************************
*
*       _cbChangeDialog
*/
static BOOL CALLBACK _cbChangeDialog(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
  WINDRIVE_INST * pInst;
  unsigned        MaxNumChars;
  int             ItemId;

  FS_USE_PARA(lParam);
  ItemId = LOWORD(wParam);
  switch (Msg) {
  case WM_INITDIALOG:
    return _OnInitChangeDialog(hWnd);
  case WM_DESTROY:
    _hWndMain = NULL;
    break;
  case WM_CLOSE:
    EndDialog(hWnd, 0);
    return FALSE;
  case WM_COMMAND:
    switch (ItemId) {
    case ID_RADIO0:
    case ID_RADIO1:
      _UpdateDialog(hWnd);
      break;
    case ID_BTN_SEL_FILE:
      _OnSelectFile(hWnd);
      break;
    case ID_BTN_CREATE_IMG:
      _OnCreateImage(hWnd);
      break;
    case IDOK:
      {
        int Item = 0;

        if (IsDlgButtonChecked(hWnd, ID_RADIO0) == BST_CHECKED) {
          Item = ID_CB_DRIVE;
        } else if (IsDlgButtonChecked(hWnd, ID_RADIO1) == BST_CHECKED) {
          Item = ID_ED_FILE;
        }
        GetDlgItemTextW(hWnd, Item, _acFileName, MAX_PATH);
        pInst = _GetInst(_UnitToConfig);
        if (pInst != NULL) {
          MaxNumChars = SEGGER_COUNTOF(pInst->acName) - 1;
          (void)wcsncpy(pInst->acName, _acFileName, MaxNumChars);
          pInst->acName[MaxNumChars] = '\0';
        }
        EndDialog(hWnd, 0);
        return FALSE;
      }
    case IDCANCEL:
      EndDialog(hWnd, 0);
      return FALSE;
    default:
      break;
    }
    break;
  default:
    break;
  }
  return FALSE;
}

/*********************************************************************
*
*       _GethInstance
*/
static HINSTANCE _GethInstance(void) {
  MEMORY_BASIC_INFORMATION mbi;

  VirtualQuery(_GethInstance, &mbi, sizeof(mbi));
  return (HINSTANCE)(mbi.AllocationBase);
}

/*********************************************************************
*
*       _LoadInfo
*/
static int _LoadInfo(U8 Unit, LPWSTR sInfo, unsigned MaxLen) {
  DWORD Type = REG_NONE;
  HKEY  hKey;
  int   r;

  r = RegCreateKey(HKEY_CURRENT_USER, REG_PATH, &hKey);
  if (r == 0) {
    WCHAR ac[10];

    FS_MEMSET(ac, 0, sizeof(ac));
    _snwprintf(ac, SEGGER_COUNTOF(ac) - 1, L"%d", (int)Unit);
    ac[SEGGER_COUNTOF(ac) - 1] = '\0';
    r = RegQueryValueExW(hKey, ac, 0, &Type, (LPBYTE)sInfo, (U32 *)&MaxLen);
    RegCloseKey(hKey);
  }
  return (r ? 1 : ((Type != REG_SZ) ? 1 : 0));
}

/*********************************************************************
*
*       _SaveInfo
*/
static int _SaveInfo(U8 Unit, LPCWSTR sInfo) {
  HKEY hKey;
  LONG Status;
  int  r;

  r = 1;        // Set to indicate and error.
  Status = RegCreateKey(HKEY_CURRENT_USER, REG_PATH, &hKey);
  if (Status == ERROR_SUCCESS) {
    WCHAR ac[10];
    DWORD NumBytes;

    FS_MEMSET(ac, 0, sizeof(ac));
    NumBytes = (DWORD)(wcslen(sInfo) * sizeof(WCHAR) + 1);
    _snwprintf(ac, SEGGER_COUNTOF(ac) - 1, L"%d", (int)Unit);
    ac[SEGGER_COUNTOF(ac) - 1] = '\0';
    Status = RegSetValueExW(hKey, (LPCWSTR)ac, 0, REG_SZ, (const BYTE *)sInfo, NumBytes);
    if (Status == ERROR_SUCCESS) {
      r = 0;
    }
    RegCloseKey(hKey);
  }
  return r;
}

/*********************************************************************
*
*       _ConfigDialog
*/
static int _ConfigDialog(U8 Unit) {
  WCHAR           ac[400];
  WINDRIVE_INST * pInst;

  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return 1;
  }
  _hDialog = _GethInstance();
  if (_hWndMain == NULL) {
    GetConsoleTitleW(ac, SEGGER_COUNTOF(ac));
    _hWndMain = FindWindowW(L"ConsoleWindowClass", ac);
    if (_hWndMain == NULL) {
      _hWndMain = GetDesktopWindow();
    }
  }
  InitCommonControls();
  _LoadInfo(Unit, _acFileName, sizeof(_acFileName));
  FS_MEMSET(ac, 0, sizeof(ac));
  _snwprintf(ac, SEGGER_COUNTOF(ac) - 1, L"win:%d: uses \"%s\" as storage.\nDo you want to keep this setting?", Unit, _acFileName);
  ac[SEGGER_COUNTOF(ac) - 1] = '\0';
  if (MessageBoxW(_hWndMain, ac, L"WinDrive Query", MB_YESNO | MB_ICONQUESTION) == IDNO) {
    _UnitToConfig = Unit;
    if (DialogBox(_hDialog, MAKEINTRESOURCE(IDD_MAINDIALOG), _hWndMain, (DLGPROC)_cbChangeDialog) == -1) {
      _ShowError(pInst, L"Cannot show dialog box", GetLastError());
      return 1;
    }
  } else {
    (void)wcsncpy(pInst->acName, _acFileName, SEGGER_COUNTOF(pInst->acName) - 1);
    pInst->acName[SEGGER_COUNTOF(pInst->acName) - 1] = '\0';
  }
  _SaveInfo(Unit, _acFileName);
  return 0;
}

/*********************************************************************
*
*       _IsVistaOrNewer
*/
static int _IsVistaOrNewer(void) {
  U32           Version;
  OSVERSIONINFO VersionInfo;
  int           r;

  r = 0;
  FS_MEMSET(&VersionInfo, 0, sizeof(VersionInfo));
  VersionInfo.dwOSVersionInfoSize = sizeof(VersionInfo);
  GetVersionEx(&VersionInfo);
  Version = VersionInfo.dwMajorVersion << 16 | VersionInfo.dwMinorVersion;
  if (Version > 0x00050000) {
    r = 1;
  }
  return r;
}

/*********************************************************************
*
*       _IsAdmin
*
*  Function description
*    Checks if the user that runs the application has administrative rights.
*
*  Return value
*    !=0      Current user has administrative rights.
*    ==0      Current user does not have administrative rights.
*/
static int _IsAdmin(WINDRIVE_INST * pInst) {
  HANDLE                   hToken;
  PSID                     pAdminSid;
  BYTE                     acBuffer[1024];
  PTOKEN_GROUPS            pGroups;
  DWORD                    Size;
  DWORD                    i;
  BOOL                     Result;
  SID_IDENTIFIER_AUTHORITY siaNtAuth = SECURITY_NT_AUTHORITY;
  int                      r;
  TOKEN_ELEVATION          Elevation;
  HANDLE                   hProcess;
  DWORD                    LastError;

  hToken    = NULL;
  pAdminSid = NULL;
  hProcess  = GetCurrentProcess();
  pGroups   = (PTOKEN_GROUPS)acBuffer;
  Result = OpenProcessToken(hProcess, TOKEN_QUERY, &hToken);
  if (Result == FALSE) {
    _ShowError(pInst, L"Cannot open token query", GetLastError());
    return 0;
  }
  //
  // Get the size of the buffer required to read the information.
  //
  Result = GetTokenInformation(hToken, TokenGroups, NULL, 0, &Size);
  if (Result == FALSE) {
    LastError = GetLastError();
    if (LastError != ERROR_INSUFFICIENT_BUFFER) {
      _ShowError(pInst, L"Cannot get size of token information", LastError);
      return 0;
    }
  }
  //
  // Allocate memory for the read buffer.
  //
  pGroups = (PTOKEN_GROUPS)malloc(Size);
  if (pGroups == NULL) {
    _ShowError(pInst, L"Cannot allocate memory", 0);
    return 0;
  }
  //
  // Read information about the groups.
  //
  Result = GetTokenInformation(hToken, TokenGroups, (LPVOID)pGroups, Size, &Size);
  CloseHandle(hToken);
  if (Result == FALSE) {
    free(pGroups);
    _ShowError(pInst, L"Cannot get token information", GetLastError());
    return 0;
  }
  //
  // Get the administrator id.
  //
  Result = AllocateAndInitializeSid(&siaNtAuth, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &pAdminSid);
  if (Result == FALSE) {
    free(pGroups);
    _ShowError(pInst, L"Cannot initialize SID", GetLastError());
    return 0;
  }
  //
  // Check if the current user has administrative rights.
  //
  r = 0;
  for (i = 0; i < pGroups->GroupCount; i++) {
    if (EqualSid(pAdminSid, pGroups->Groups[i].Sid)) {
      r = 1;
      break;
    }
  }
  free(pGroups);
  FreeSid(pAdminSid);
  if (r == 0) {
    //
    // Check if the application was started with administrative rights.
    //
    if (_IsVistaOrNewer() != 0) {
      hToken = NULL;
      Result = OpenProcessToken(hProcess, TOKEN_QUERY, &hToken);
      if (Result == FALSE) {
        _ShowError(pInst, L"Cannot open token query", GetLastError());
        return 0;
      }
      Size = sizeof(TOKEN_ELEVATION);
      Result = GetTokenInformation(hToken, TokenElevation, &Elevation, sizeof(Elevation), &Size);
      CloseHandle(hToken);
      if (Result == FALSE) {
        _ShowError(pInst, L"Cannot get elevation information", GetLastError());
        return 0;
      }
      r = 0;
      if (Elevation.TokenIsElevated != 0) {
        r = 1;
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _GetSectorSize
*/
static U32 _GetSectorSize(WINDRIVE_INST * pInst) {
  DISK_GEOMETRY DiskGeometry;
  DWORD         Size;
  BOOL          Result;
  U32           BytesPerSector;

  BytesPerSector = 0;
  Size = sizeof(DiskGeometry);
  FS_MEMSET(&DiskGeometry, 0, Size);
  Result = DeviceIoControl(pInst->hStorage, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &DiskGeometry, Size, &Size, NULL);
  if (Result == TRUE) {
    BytesPerSector = DiskGeometry.BytesPerSector;
  }
  return BytesPerSector;
}

/*********************************************************************
*
*       _RequestExclusiveAccess
*/
static int _RequestExclusiveAccess(WINDRIVE_INST * pInst) {
  BOOL   Result;
  WCHAR  ac[256];
  U32    Dummy;
  HANDLE hStorage;

  hStorage = pInst->hStorage;
  Result = DeviceIoControl(hStorage, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &Dummy, NULL);
  if (Result == FALSE) {
    _snwprintf(ac, SEGGER_COUNTOF(ac), L"Could not dismount volume \"%s\"", pInst->acName);
    _ShowWarning(pInst, ac);
    return 1;
  }
  Result = DeviceIoControl(hStorage, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &Dummy, NULL);
  if (Result == FALSE) {
    _snwprintf(ac, SEGGER_COUNTOF(ac), L"Could not lock volume \"%s\"", pInst->acName);
    _ShowWarning(pInst, ac);
    return 1;
  }
  return 0;
}

/*********************************************************************
*
*       _Init
*/
static int _Init(WINDRIVE_INST * pInst) {
  U32     BytesPerSector;
  WCHAR   ac[256];
  int     r;
  DWORD   DesiredAccess;
  DWORD   ShareMode;
  DWORD   CreationDisposition;
  DWORD   FlagsAndAttributes;
  U32     NumSectors;
  WCHAR * sName;
  U32     FileSize;
  U32     FileSizeAct;

  if (pInst->hStorage != INVALID_HANDLE_VALUE) {
    return 0;           // OK, instance already initialized.
  }
  if (_IsAdmin(pInst) == 0) {
    _ShowError(pInst, L"Administrative rights are required to open a volume.\n Please re-run the application as administrator", 0);
    return 1;           // Error, the user does not have administrator privileges.
  }
  NumSectors     = pInst->NumSectors;
  sName          = pInst->acName;
  BytesPerSector = pInst->BytesPerSector;
  if (wcslen(sName) == 0) {
    _ShowError(pInst, L"Invalid drive or file name", 0);
    return 1;           // Error, no Windows drive or image file name specified.
  }
  //
  // Try to open the drive or the image file.
  //
  DesiredAccess       = GENERIC_READ | GENERIC_WRITE;
  ShareMode           = FILE_SHARE_READ | FILE_SHARE_WRITE;
  CreationDisposition = OPEN_EXISTING;
  FlagsAndAttributes  = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH | FILE_ATTRIBUTE_DEVICE;
  pInst->hStorage = CreateFileW(sName, DesiredAccess, ShareMode, NULL, CreationDisposition, FlagsAndAttributes, NULL);
  if (pInst->hStorage == INVALID_HANDLE_VALUE) {
    if (NumSectors != 0u) {
      //
      // Try to create the file if the user specified a storage capacity.
      //
      r = _CreateImageFile(pInst, sName, NumSectors, BytesPerSector);
      if (r == 0) {
        //
        // We have to open the image file with the buffering enabled so that we can read less than 512 bytes from it.
        //
        FlagsAndAttributes &= ~FILE_FLAG_NO_BUFFERING;   
        pInst->hStorage = CreateFileW(sName, DesiredAccess, ShareMode, NULL, CreationDisposition, FlagsAndAttributes, NULL);
      }
    }
    if (pInst->hStorage == INVALID_HANDLE_VALUE) {
      _snwprintf(ac, SEGGER_COUNTOF(ac), L"Could not open a handle to \"%s\"", sName);
      _ShowError(pInst, ac, GetLastError());
      return 1;           // Error, could not open the handle.
    }
  }
  BytesPerSector = _GetSectorSize(pInst);
  if (BytesPerSector != 0) {
    //
    // This is a drive.
    //
    pInst->IsDrive = 1;
  } else {
    //
    // This is an image file.
    //
    if (NumSectors == 0u) {
      BytesPerSector = FS_WINDRIVE_SECTOR_SIZE;
    } else {
      BytesPerSector = pInst->BytesPerSector;
      FileSize       = NumSectors * BytesPerSector;
      FileSizeAct    = GetFileSize(pInst->hStorage, NULL);
      if (FileSize != FileSizeAct) {
        CloseHandle(pInst->hStorage);
        r = _CreateImageFile(pInst, sName, NumSectors, BytesPerSector);
        if (r == 0) {
          //
          // We have to open the image file with the buffering enabled so that we can read less than 512 bytes from it.
          //
          FlagsAndAttributes &= ~FILE_FLAG_NO_BUFFERING;   
          pInst->hStorage = CreateFileW(sName, DesiredAccess, ShareMode, NULL, CreationDisposition, FlagsAndAttributes, NULL);
          if (pInst->hStorage == INVALID_HANDLE_VALUE) {
            _snwprintf(ac, SEGGER_COUNTOF(ac), L"Could not open a handle to \"%s\"", sName);
            _ShowError(pInst, ac, GetLastError());
            return 1;           // Error, could not open the file.
          }
        }
      } else {
        //
        // Reopen the image file with the buffering enabled so that we can read less than 512 bytes from it.
        //
        CloseHandle(pInst->hStorage);
        FlagsAndAttributes &= ~FILE_FLAG_NO_BUFFERING;   
        pInst->hStorage = CreateFileW(sName, DesiredAccess, ShareMode, NULL, CreationDisposition, FlagsAndAttributes, NULL);
        if (pInst->hStorage == INVALID_HANDLE_VALUE) {
          _snwprintf(ac, SEGGER_COUNTOF(ac), L"Could not open a handle to \"%s\"", sName);
          _ShowError(pInst, ac, GetLastError());
          return 1;           // Error, could not open the file.
        }
      }
    }
  }
  pInst->BytesPerSector = BytesPerSector;
  return 0;
}

/*********************************************************************
*
*       _InitIfRequired
*/
static int _InitIfRequired(WINDRIVE_INST * pInst) {
  int r;

  r = 0;
  if (pInst->hStorage == INVALID_HANDLE_VALUE) {
    r = _Init(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _AllocInstIfRequired
*
*  Function description
*    Allocates memory for the instance of a driver.
*/
static WINDRIVE_INST * _AllocInstIfRequired(U8 Unit) {
  WINDRIVE_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = NULL;
  if (Unit < (U8)FS_WINDRIVE_NUM_UNITS) {
    pInst = _apInst[Unit];
    if (pInst == NULL) {
      pInst = SEGGER_PTR2PTR(WINDRIVE_INST, FS_ALLOC_ZEROED(sizeof(WINDRIVE_INST), "WINDRIVE_INST"));
      if (pInst != NULL) {
        pInst->hStorage = INVALID_HANDLE_VALUE;
        _apInst[Unit]   = pInst;
      }
    }
  }
  return pInst;
}

/*********************************************************************
*
*       _Read
*
*  Function description
*    FS driver function. Read sector(s) from the storage device.
*
*  Parameters
*    pInst        Driver instance.
*    SectorIndex  Sector to be read from the device.
*    pData        Pointer to data.
*    NumSectors   Number of sectors to read.
*
*  Return value
*    ==0    Sector has been written to the device.
*    < 0    An error has occurred.
*/
static int _Read(WINDRIVE_INST * pInst, U32 SectorIndex, void * pData, U32 NumSectors) {
  U32             NumBytesRead;
  U32             NumBytesToRead;
  U32             BytesPerSector;
  LARGE_INTEGER   FilePos;
  HANDLE          hStorage;
  U32             FilePosLow;
  BOOL            Result;
  DWORD           LastError;
  void          * pBuffer;
  int             r;
  U32             BufferAlignment;

  hStorage        = pInst->hStorage;
  BytesPerSector  = pInst->BytesPerSector;
  NumBytesToRead  = BytesPerSector * NumSectors;
  //
  // Update the read position.
  //
  FilePos.QuadPart = (__int64)SectorIndex * (__int64)BytesPerSector;
  FilePosLow = SetFilePointer(hStorage, (LONG)FilePos.LowPart, &FilePos.HighPart, FILE_BEGIN);
  if (FilePosLow == INVALID_SET_FILE_POINTER) {
    _ShowError(pInst, L"Could not set position for reading", GetLastError());
    return 1;
  }
  //
  // Read the data.
  //
  r       = 1;                        // Set to indicate error.
  pBuffer = pData;
  for (;;) {
    BufferAlignment = pInst->DataBufferAlignment;
    //
    // Copy the data to the internal buffer if one is allocated.
    //
    if (BufferAlignment != 0) {
      pBuffer = _AllocDataBuffer(pInst, NumBytesToRead);
      if (pBuffer == NULL) {
        _ShowError(pInst, L"Could allocate data buffer", 0);
        break;
      }
    }
    NumBytesRead = 0;
    Result = ReadFile(hStorage, pBuffer, NumBytesToRead, &NumBytesRead, NULL);
    if ((Result == TRUE) && (NumBytesToRead == NumBytesRead)) {
      if (pBuffer != NULL) {
        FS_MEMCPY(pData, pBuffer, NumBytesRead);
      }
      r = 0;
      break;                          // OK, data read.
    }
    LastError = GetLastError();
    if ((LastError != ERROR_INVALID_PARAMETER) || (BufferAlignment != 0)) {
      _ShowError(pInst, L"Could not read", LastError);
      break;
    }
    //
    // Retry the write operation with an aligned buffer.
    //
    pInst->DataBufferAlignment = BytesPerSector;
  }
  return r;
}

/*********************************************************************
*
*       _Write
*
*  Function description
*    FS driver function. Write sector(s) to the storage device.
*
*  Parameters
*    pInst        Driver instance.
*    SectorIndex  Sector to be written to the device.
*    pData        Pointer to data to be stored.
*    NumSectors   Number of sectors to write.
*
*  Return value
*    ==0    Sector has been written to the device.
*    < 0    An error has occurred.
*/
static int _Write(WINDRIVE_INST * pInst, U32 SectorIndex, const void * pData, U32 NumSectors) {
  U32             NumBytesWritten;
  U32             NumBytesToWrite;
  U32             BytesPerSector;
  LARGE_INTEGER   FilePos;
  HANDLE          hStorage;
  U32             FilePosLow;
  BOOL            Result;
  DWORD           LastError;
  void          * pBuffer;
  int             r;
  U32             BufferAlignment;

  hStorage        = pInst->hStorage;
  BytesPerSector  = pInst->BytesPerSector;
  NumBytesToWrite = BytesPerSector * NumSectors;
  //
  // Update the write position.
  //
  FilePos.QuadPart = (__int64)SectorIndex * (__int64)BytesPerSector;
  FilePosLow = SetFilePointer(hStorage, (LONG)FilePos.LowPart, &FilePos.HighPart, FILE_BEGIN);
  if (FilePosLow == INVALID_SET_FILE_POINTER) {
    _ShowError(pInst, L"Could not set position for writing", GetLastError());
    return 1;
  }
  //
  // Write the data.
  //
  r       = 1;                        // Set to indicate error.
  pBuffer = NULL;
  for (;;) {
    BufferAlignment = pInst->DataBufferAlignment;
    //
    // Copy the data to the internal buffer if one is allocated.
    //
    if (BufferAlignment != 0) {
      pBuffer = _AllocDataBuffer(pInst, NumBytesToWrite);
      if (pBuffer == NULL) {
        _ShowError(pInst, L"Could allocate data buffer", 0);
        break;
      }
      FS_MEMCPY(pBuffer, pData, NumBytesToWrite);
      pData = pBuffer;
    }
    NumBytesWritten = 0;
    Result = WriteFile(hStorage, pData, NumBytesToWrite, &NumBytesWritten, NULL);
    if ((Result == TRUE) && (NumBytesToWrite == NumBytesWritten)) {
      r = 0;
      break;                          // OK, data written.
    }
    LastError = GetLastError();
    if ((LastError != ERROR_INVALID_PARAMETER) || (BufferAlignment != 0)) {
      _ShowError(pInst, L"Could not write", LastError);
      break;
    }
    //
    // Retry the write operation with an aligned buffer.
    //
    pInst->DataBufferAlignment = BytesPerSector;
  }
  return r;
}

/*********************************************************************
*
*       _GetDeviceInfoDrive
*/
static int _GetDeviceInfoDrive(WINDRIVE_INST * pInst, FS_DEV_INFO * pDeviceInfo) {
  BOOL                   Result;
  DWORD                  Size;
  GET_LENGTH_INFORMATION LenInfo;
  U32                    BytesPerSector;
  U32                    NumSectors;
  U32                    NumBytesHigh;
  U32                    NumBytesLow;
  DISK_GEOMETRY          DiskGeometry;
  U32                    NumCylinders;
  U32                    SectorsPerTrack;
  U32                    TracksPerCylinder;

  BytesPerSector = pInst->BytesPerSector;
  //
  // Calculate the number of sectors using the volume size.
  //
  Size = sizeof(LenInfo);
  FS_MEMSET(&LenInfo, 0, Size);
  Result = DeviceIoControl(pInst->hStorage, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &LenInfo, Size, &Size, NULL);
  if (Result == TRUE) {
    NumBytesLow  = (U32)LenInfo.Length.LowPart;
    NumBytesHigh = (U32)LenInfo.Length.HighPart;
    NumSectors = (U32)(((U64)NumBytesHigh << 32 | (U64)NumBytesLow) / BytesPerSector);
    pDeviceInfo->NumSectors      = NumSectors;
    pDeviceInfo->BytesPerSector  = (U16)BytesPerSector;
    pDeviceInfo->NumHeads        = 63;
    pDeviceInfo->SectorsPerTrack = 255;
    return 0;
  }
  //
  // Fall back to the old method of calculating the number of sectors.
  //
  Size = sizeof(DiskGeometry);
  FS_MEMSET(&DiskGeometry, 0, Size);
  Result = DeviceIoControl(pInst->hStorage, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &DiskGeometry, Size, &Size, NULL);
  if (Result == FALSE) {
    _ShowError(pInst, L"Cannot get device geometry", GetLastError());
    return 1;
  }
  NumCylinders      = (U32)DiskGeometry.Cylinders.QuadPart;
  SectorsPerTrack   = DiskGeometry.SectorsPerTrack;
  TracksPerCylinder = DiskGeometry.TracksPerCylinder;
  if (SectorsPerTrack == 63) {
    //
    // Some storage devices such SD cards report inaccurate values. Since we can not read the number
    // of sectors from the card info structure, we have to estimate: -6%.
    //
    NumCylinders = (NumCylinders + 1) & ~1;
    NumSectors = NumCylinders * SectorsPerTrack * TracksPerCylinder;
    NumSectors = (U32)(((__int64)NumSectors * 94) / 100);
  } else {
    NumSectors = NumCylinders * SectorsPerTrack * TracksPerCylinder;
  }
  pDeviceInfo->NumSectors      = NumSectors;
  pDeviceInfo->BytesPerSector  = (U16)BytesPerSector;
  pDeviceInfo->NumHeads        = (U16)TracksPerCylinder;
  pDeviceInfo->SectorsPerTrack = (U16)SectorsPerTrack;
  return 0;
}

/*********************************************************************
*
*       _GetDeviceInfoImage
*/
static int _GetDeviceInfoImage(WINDRIVE_INST * pInst, FS_DEV_INFO * pDeviceInfo) {
  U32 NumBytesHigh;
  U32 NumBytesLow;
  U32 BytesPerSector;
  U32 NumSectors;

  BytesPerSector = pInst->BytesPerSector;
  NumBytesLow    = GetFileSize(pInst->hStorage, &NumBytesHigh);
  if (NumBytesLow == INVALID_FILE_SIZE) {
    _ShowError(pInst, L"Could not get file size", GetLastError());
    return 1;
  }
  NumSectors = (U32)(((U64)NumBytesHigh << 32 | (U64)NumBytesLow) / BytesPerSector);
  pDeviceInfo->NumSectors      = NumSectors;
  pDeviceInfo->BytesPerSector  = (U16)BytesPerSector;
  pDeviceInfo->NumHeads        = 63;
  pDeviceInfo->SectorsPerTrack = 255;
  return 0;
}

/*********************************************************************
*
*       _GetDeviceInfo
*/
static int _GetDeviceInfo(WINDRIVE_INST * pInst, FS_DEV_INFO * pDeviceInfo) {
  int r;

  if (pInst->IsDrive != 0) {
    r = _GetDeviceInfoDrive(pInst, pDeviceInfo);
  } else {
    r = _GetDeviceInfoImage(pInst, pDeviceInfo);
  }
  return r;
}

/*********************************************************************
*
*       _Lock
*/
static int _Lock(WINDRIVE_INST * pInst) {
  int r;

  r = 0;
  if (pInst->IsDrive != 0) {
    if (_IsVistaOrNewer() != 0) {
      //
      // In order to use WinDrive driver with Windows Vista and Windows 7, we need
      // to exclusively lock the volume otherwise we will not be able to perform
      // any write operation on that volume.
      //
      r = _RequestExclusiveAccess(pInst);
      if (r == 0) {
        pInst->IsLocked = 1;
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _LockIfRequired
*/
static int _LockIfRequired(WINDRIVE_INST * pInst) {
  int r;

  r = 0;
  if (pInst->IsLocked == 0) {
    r = _Lock(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _Unlock
*/
static int _Unlock(WINDRIVE_INST * pInst) {
  BOOL  Result;
  DWORD Size;
  int   r;

  r = 0;
  Result = DeviceIoControl(pInst->hStorage, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &Size, NULL);
  if (Result == FALSE) {
    r = 1;
  } else {
    pInst->IsLocked = 0;
  }
  return r;
}

/*********************************************************************
*
*       _UnlockIfRequired
*/
static int _UnlockIfRequired(WINDRIVE_INST * pInst) {
  int r;

  r = 0;
  if (pInst->IsLocked != 0) {
    r = _Unlock(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _DeInitIfRequired
*/
static int _DeInitIfRequired(WINDRIVE_INST * pInst) {
  int  r;
  BOOL Result;

  r = 0;
  if (pInst->hStorage != INVALID_HANDLE_VALUE) {
    Result = CloseHandle(pInst->hStorage);
    if (Result == FALSE) {
      r = 1;
    } else {
      pInst->hStorage = INVALID_HANDLE_VALUE;
    }
  }
  return r;
}

/*********************************************************************
*
*       Static code (public via callback)
*
**********************************************************************
*/

/*********************************************************************
*
*       _WINDRIVE_Read
*
*  Function description
*    FS driver function. Read a sector from the media.
*
*  Parameters
*    Unit         Device number.
*    SectorIndex  Sector to be read from the device.
*    pData        Pointer to buffer for storing the data.
*    NumSectors   Number of sectors to read.
*
*  Return value
*    ==0    Sector has been read and copied to pBuffer.
*    !=0    An error has occurred.
*/
static int _WINDRIVE_Read(U8 Unit, U32 SectorIndex, void * pData, U32 NumSectors) {
  WINDRIVE_INST * pInst;
  int             r;

  r = 1;                    // Set to indicate an error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    r = _LockIfRequired(pInst);
    if (r == 0) {
      r = _Read(pInst, SectorIndex, pData, NumSectors);
    }
  }
  return r;
}

/*********************************************************************
*
*       _WINDRIVE_Write
*
*  Function description
*    FS driver function. Write sector to the media.
*
*  Parameters
*    Unit         Device number.
*    SectorIndex  Sector to be written to the device.
*    pData        Pointer to data to be stored.
*    NumSectors   Number of sectors to write.
*    RepeatSame   Set to 1 if the same data has to be written to all sectors.
*                 pBuffer points to the contents of a single sector.
*
*  Return value
*    ==0    Sector has been written to the device.
*    !=0    An error has occurred.
*/
static int _WINDRIVE_Write(U8 Unit, U32 SectorIndex, const void * pData, U32 NumSectors, U8 RepeatSame) {
  U8            * pBuffer;
  U32             i;
  int             r;
  U32             BytesPerSector;
  WINDRIVE_INST * pInst;
  U32             NumBytes;

  r = 1;            // Set to indicate error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    r = _LockIfRequired(pInst);
    if (RepeatSame != 0u) {
      BytesPerSector  = pInst->BytesPerSector;
      NumBytes = NumSectors * BytesPerSector;
      //
      // Try to write all the sectors at once using a dynamically allocated buffer.
      //
      if (pInst->DataBufferAlignment != 0) {
        pBuffer = (U8 *)_AllocDataBuffer(pInst, NumBytes);
      } else {
        pBuffer = (U8 *)_aligned_malloc(NumBytes, BytesPerSector);
      }
      if (pBuffer != NULL) {
        for (i = 0; i < NumSectors; i++) {
          FS_MEMCPY(pBuffer + i * BytesPerSector, pData, BytesPerSector);
        }
        r = _Write(pInst, SectorIndex, pBuffer, NumSectors);
        if (pInst->DataBufferAlignment == 0) {
          _aligned_free(pBuffer);
        }
      } else {
        //
        // Write the sectors one by one.
        //
        for (i = 0; i < NumSectors; i++) {
          r = _Write(pInst, SectorIndex++, pData, 1);
          if (r != 0) {
            break;
          }
        }
      }
    } else {
      r = _Write(pInst, SectorIndex, pData, NumSectors);
    }
  }
  return r;
}

/*********************************************************************
*
*       _WINDRIVE_GetStatus
*
*  Function description
*    FS driver function. Get status of the media.
*
*  Parameters
*    Unit     Index of the driver instance.
*
*  Return value
*    FS_MEDIA_STATE_UNKNOWN   The presence status of the storage device is unknown.
*    FS_MEDIA_NOT_PRESENT     The storage device is not present.
*    FS_MEDIA_IS_PRESENT      The storage device is present.
*/
static int _WINDRIVE_GetStatus(U8 Unit) {
  WINDRIVE_INST * pInst;
  int             r;
  int             rInit;
  U32             Dummy;
  BOOL            Result;
  int             IsInited;

  r = FS_MEDIA_NOT_PRESENT;
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    //
    // Remember if the storage device was already initialized.
    //
    IsInited = 0;
    if (pInst->hStorage != INVALID_HANDLE_VALUE) {
      IsInited = 1;
    }
    pInst->SuppressErrors   = 1;
    pInst->SuppressWarnings = 1;
    rInit = _InitIfRequired(pInst);
    pInst->SuppressErrors   = 0;
    pInst->SuppressWarnings = 0;
    if (rInit == 0) {
      if (pInst->IsDrive != 0u) {
        Result = DeviceIoControl(pInst->hStorage, IOCTL_STORAGE_CHECK_VERIFY, NULL, 0, NULL, 0, &Dummy, NULL);
        if (Result == TRUE) {
          r = FS_MEDIA_IS_PRESENT;
        }
      } else {
        r = FS_MEDIA_IS_PRESENT;
      }
      //
      // Close the handle to storage device after the check
      // if the handle was opened only for the checking operation.
      //
      if (IsInited == 0) {
        (void)_DeInitIfRequired(pInst);
      }
    }
  }
  return r;
}

/*********************************************************************
*
*       _WINDRIVE_IoCtl
*
*  Function description
*    FS driver function. Execute device command.
*
*  Parameters
*    Unit       Index of the driver instance.
*    Cmd        Command to be executed.
*    Aux        Parameter depending on command.
*    pBuffer    Pointer to a buffer used for the command.
*
*  Return value
*    Command specific. In general a negative value means an error.
*/
static int _WINDRIVE_IoCtl(U8 Unit, I32 Cmd, I32 Aux, void * pBuffer) {
  FS_DEV_INFO   * pInfo;
  WINDRIVE_INST * pInst;
  int             r;

  FS_USE_PARA(Aux);
  pInst = _GetInst(Unit);
  if (pInst == NULL) {
    return -1;                      // Error, driver instance not found.
  }
  r = -1;                           // Set to indicate an error.
  switch (Cmd) {
  case FS_CMD_GET_DEVINFO:
    if (pBuffer != NULL) {
      r = _InitIfRequired(pInst);
      if (r == 0) {
        pInfo = (FS_DEV_INFO *)pBuffer;
        r = _GetDeviceInfo(pInst, pInfo);
      }
    }
    break;
  case FS_CMD_UNMOUNT:
  case FS_CMD_UNMOUNT_FORCED:
    (void)_UnlockIfRequired(pInst);
    (void)_DeInitIfRequired(pInst);
    _FreeDataBuffer(pInst);
    r = 0;
    break;
#if FS_SUPPORT_DEINIT
  case FS_CMD_DEINIT:
    (void)_UnlockIfRequired(pInst);
    (void)_DeInitIfRequired(pInst);
    _FreeDataBuffer(pInst);
    FS_FREE(pInst);
    _apInst[Unit] = NULL;
    _NumUnits--;
    r = 0;
    break;
#endif // FS_SUPPORT_DEINIT
  case FS_CMD_FREE_SECTORS:
    //
    // Return OK even if we do nothing here in order to
    // prevent that the file system reports an error.
    //
    r = 0;
    break;
  default:
    break;
  }
  return r;
}

/*********************************************************************
*
*       _WINDRIVE_InitMedium
*
*  Function description
*    Initialize the specified medium.
*
*  Parameters
*    Unit   Index of the driver instance.
*
*  Return value
*    ==0    OK, driver initialized.
*    !=0    An error occurred.
*/
static int _WINDRIVE_InitMedium(U8 Unit) {
  WINDRIVE_INST * pInst;
  int             r;

  r = 1;            // Set to indicate error.
  pInst = _GetInst(Unit);
  if (pInst != NULL) {
    r = _InitIfRequired(pInst);
  }
  return r;
}

/*********************************************************************
*
*       _WINDRIVE_AddDevice
*
*  Function description
*    Initializes the driver instance.
*
*  Return value
*    >= 0   OK, unit number of the allocated driver instance.
*    <  0   Error, could not add device.
*/
static int _WINDRIVE_AddDevice(void) {
  U8              Unit;
  WINDRIVE_INST * pInst;

  Unit = (U8)_NumUnits;
  pInst = _AllocInstIfRequired(Unit);
  if (pInst == NULL) {
    return -1;                    // Error, too many driver instances.
  }
  _NumUnits++;
  return Unit;
}

/*********************************************************************
*
*       _WINDRIVE_GetNumUnits
*/
static int _WINDRIVE_GetNumUnits(void) {
  return _NumUnits;
}

/*********************************************************************
*
*       _WINDRIVE_GetDriverName
*/
static const char * _WINDRIVE_GetDriverName(U8 Unit) {
  FS_USE_PARA(Unit);
  return "win";
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_WINDRIVE_Configure
*
*  Function description
*    Configures a driver instance.
*
*  Parameters
*    Unit     Index of the instance to configure (0-based)
*    sName    [IN] Name of the Windows drive or of the image file
*             to be used as storage. Can be NULL.
*
*  Additional information
*    Either FS_WINDRIVE_Configure() or FS_WINDRIVE_ConfigureEx()
*    has to be called once for each instance of the WINDRIVE driver.
*    sName is a 0-terminated wide char string that stores the path
*    to the Windows drive or to the image file to be used as storage.
*
*    If sName is set to NULL the driver shows a dialog box that
*    allows the user to select a specific drive from a list a list
*    of available Windows drives. If sName is a path to a regular
*    file that file has to exists before FS_WINDRIVE_ConfigureEx()
*    is called. Selecting a Windows drive as storage requires
*    administrator privileges. The file system reports an error
*    to the application if this is not the case and the application
*    will not be able to access the to use the Windows drive as storage.
*
*    The size of the logical sector used by the WinDrive driver
*    can be configured at compile time via FS_WINDRIVE_SECTOR_SIZE
*    or at runtime via FS_WINDRIVE_SetGeometry().
*/
void FS_WINDRIVE_Configure(U8 Unit, const char * sName) {
  WINDRIVE_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    if ((sName == NULL) || (*sName == '\0')) {
      pInst->IsInteractive = 1;
      _ConfigDialog(Unit);
    } else {
      WCHAR    * pacName;
      int        NumChars;
      unsigned   NumBytes;

      //
      // Convert the ASCII string to wide char and then copy it to driver instance.
      //
      NumChars = MultiByteToWideChar(CP_ACP, 0, sName, -1, NULL , 0);
      if (NumChars > 0) {
        NumBytes = (unsigned)NumChars * sizeof(WCHAR);
        pacName  = (WCHAR *)malloc(NumBytes);
        if (pacName != NULL) {
          (void)MultiByteToWideChar(CP_ACP, 0, sName, -1, pacName, NumChars);
          (void)wcsncpy(pInst->acName, pacName, SEGGER_COUNTOF(pInst->acName) - 1u);
          pInst->acName[SEGGER_COUNTOF(pInst->acName) - 1u] = '\0';
          free(pacName);
        }
      }
    }
  }
}

/*********************************************************************
*
*       FS_WINDRIVE_ConfigureEx
*
*  Function description
*    Configures a driver instance.
*
*  Parameters
*    Unit     Index of the instance to configure (0-based)
*    sName    [IN] Name of the Windows drive or of the image file
*             to be used as storage. Can be NULL.
*
*  Additional information
*    This function performs the same operation as FS_WINDRIVE_Configure()
*    with the difference that sName is a pointer to a 0-terminated
*    string containing wide characters. FS_WINDRIVE_ConfigureEx() has
*    to be used when the path to the Windows drive or image file
*    can contain non-ASCII characters.
*
*    sName has to point to 0-terminated string containing wide characters.
*    A string literal can be declared in C by prefixing it with
*    the 'L' character. For example the path to the drive with the
*    letter 'E' can be specified as \tt{L"\\\\\\\\.\\\\E:"}
*
*    If sName is set to NULL the driver shows a dialog box that
*    allows the user to select a specific drive from a list a list
*    of available Windows drives. If sName is a path to a regular
*    file that file has to exists before FS_WINDRIVE_ConfigureEx()
*    is called. Selecting a Windows drive as storage requires
*    administrator privileges. The file system reports an error
*    to the application if this is not the case and the application
*    will not be able to access the to use the Windows drive as storage.
*
*    The size of the logical sector used by the  WINDRIVE driver
*    can be configured at compile time via FS_WINDRIVE_SECTOR_SIZE
*    or at runtime via FS_WINDRIVE_SetGeometry().
*/
void FS_WINDRIVE_ConfigureEx(U8 Unit, LPCWSTR sName) {
  WINDRIVE_INST * pInst;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    if ((sName == NULL) || (*sName == '\0')) {
      pInst->IsInteractive = 1;
      _ConfigDialog(Unit);
    } else {
      (void)wcsncpy(pInst->acName, sName, SEGGER_COUNTOF(pInst->acName) - 1u);
      pInst->acName[SEGGER_COUNTOF(pInst->acName) - 1u] = '\0';
    }
  }
}

/*********************************************************************
*
*       FS_WINDRIVE_SetGeometry
*
*  Function description
*    Configures the storage capacity of an image file.
*
*  Parameters
*    Unit             Index of the instance to configure (0-based)
*    BytesPerSector   Number of bytes in a logical sector. Has to be a power of 2 value.
*    NumSectors       Number of logical sectors that can be stored to the image file.
*
*  Return value
*    ==0    OK, parameters set.
*    !=0    Error code indicating the failure reason.
*
*  Additional information
*    This function is optional. When not called the driver uses the
*    sector size configured via FS_WINDRIVE_SECTOR_SIZE. The number of
*    sectors is calculated by dividing the size of the image file to
*    FS_WINDRIVE_SECTOR_SIZE. This implies that by default the driver
*    fails to initialize if the image file is missing.
*
*    Calling FS_WINDRIVE_SetGeometry() changes the behavior of the
*    driver during initialization in that the driver will try to
*    create the image file if missing. In addition, if an image file
*    is present the driver checks verifies if the size of the image
*    file matches the size configured via FS_WINDRIVE_SetGeometry()
*    and if not it recreates the image file.
*
*    The size of the image file in bytes is NumSectors * BytesPerSector.
*    Image files larger than or equal to 4 Gbytes are not supported.
*/
int FS_WINDRIVE_SetGeometry(U8 Unit, U32 BytesPerSector, U32 NumSectors) {
  WINDRIVE_INST * pInst;
  int             r;

  ASSERT_UNIT_NO_IS_IN_RANGE(Unit);
  r = FS_ERRCODE_INVALID_PARA;
  pInst = _AllocInstIfRequired(Unit);
  if (pInst != NULL) {
    if ((NumSectors != 0u) && (BytesPerSector != 0u)) {
      pInst->NumSectors     = NumSectors;
      pInst->BytesPerSector = BytesPerSector;
      r = FS_ERRCODE_OK;
    }
  }
  return r;
}

/*********************************************************************
*
*       Public const data
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_WINDRIVE_Driver
*/
const FS_DEVICE_TYPE FS_WINDRIVE_Driver = {
  _WINDRIVE_GetDriverName,
  _WINDRIVE_AddDevice,
  _WINDRIVE_Read,
  _WINDRIVE_Write,
  _WINDRIVE_IoCtl,
  _WINDRIVE_InitMedium,
  _WINDRIVE_GetStatus,
  _WINDRIVE_GetNumUnits
};

#else

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_WinDrive_c
*
*  Function description
*    Dummy function to prevent compiler errors.
*/
void FS_WinDrive_c(void);
void FS_WinDrive_c(void) {
  ;
}

#endif  // _WIN32

/*************************** End of file ****************************/
