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
File        : FS_CLib.c
Purpose     : Standard C library replacement routines.
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "FS_Int.h"

/*********************************************************************
*
*       Defines, fixed
*
**********************************************************************
*/
#define UPPER_CASE_OFF        0x20

#if defined(_WIN32) || defined(__linux__)
  #define SIMULATOR_RUN       1
#else
  #define SIMULATOR_RUN       0
#endif

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

//lint -efunc(662, FS__CLIB_memset) possible creation of out-of-bounds pointer N:100. Rationale: The function was manually verified to work correctly.
//lint -efunc(661, FS__CLIB_memset) possible access of out-of-bounds pointer N:100. Rationale: The function was manually verified to work correctly.
//lint -efunc(797, FS__CLIB_memset) Conceivable creation of out-of-bounds pointer N:100. Rationale: The function was manually verified to work correctly.
//lint -efunc(796, FS__CLIB_memset) Conceivable access of out-of-bounds pointer N:100. Rationale: The function was manually verified to work correctly.

/*********************************************************************
*
*       FS__CLIB_memcmp
*
*  Function description
*    Compare bytes in two buffers
*
*  Parameters
*    s1         Pointer to first buffer.
*    s2         Pointer to second buffer.
*    NumBytes   Number of bytes to compare.
*
*  Return value
*    ==0    Bytes are equal
*    !=0    Bytes are different
*/
int FS__CLIB_memcmp(const void * s1, const void * s2, unsigned NumBytes) {
  const U8 * p1;
  const U8 * p2;
  unsigned   v1;
  unsigned   v2;
  int        r;

  p1 = SEGGER_CONSTPTR2PTR(const U8, s1);     // MISRA deviation D:100[e]
  p2 = SEGGER_CONSTPTR2PTR(const U8, s2);     // MISRA deviation D:100[e]
  while (NumBytes-- != 0u) {
    v1 = *p1;
    v2 = *p2;
    r = (int)v1 - (int)v2;
    if (r != 0) {
      return r;                               // Not equal
    }
    ++p1;
    ++p2;
  }
  return 0;                                   // Equal
}

/*********************************************************************
*
*       FS__CLIB_memset
*
*  Function description
*    FS internal function. Copy the value of Fill (converted to an unsigned
*    char) into each of the first NumBytes characters of the object pointed to
*    by pData.
*
*  Parameters
*    pData      Pointer to an object.
*    Fill       Value to be set.
*    NumBytes   Number of characters to be set.
*
*  Return value
*    Value of pData.
*/
void * FS__CLIB_memset(void * pData, int Fill, U32 NumBytes) {
  U8  * p;
  int   NumInts;

  p = SEGGER_PTR2PTR(U8, pData);                                                      // MISRA deviation D:100[e]
  //
  // Write bytes until we are done or have reached an int boundary
  //
  while ((NumBytes != 0u) && (((sizeof(int) - 1u) & SEGGER_PTR2ADDR(p)) != 0u)) {     // MISRA deviation D:100[e]
    *p++ = (U8)Fill;
    NumBytes--;
  }
  //
  // Write one integer at a time.
  //
  NumInts = (int)NumBytes / (int)sizeof(int);
  if (NumInts > 0) {
    int   FillInt;
    int * pInt;

    NumBytes &= (sizeof(int) - 1u);
    if (sizeof(int) == 2u) {                                                          //lint !e506 !e774 Constant value Boolean and boolean always evaluates to [True/False] N:999.
      FillInt = Fill * 0x101;                                                         // Some compilers may generate a warning at this line: Unreachable code
    } else {
      if (sizeof(int) == 4u) {                                                        //lint !e506 !e774 Constant value Boolean and boolean always evaluates to [True/False] N:999.
        FillInt = Fill * 0x1010101;                                                   // Some compilers may generate a warning at this line: Unreachable code
      } else {
        FillInt = 0;
      }
    }
    pInt = SEGGER_PTR2PTR(int, p);                                                    // MISRA deviation D:100[e]
    //
    // Fill large amount of data at a time.
    //
    while (NumInts >= 4) {
      *pInt++ = FillInt;
      *pInt++ = FillInt;
      *pInt++ = FillInt;
      *pInt++ = FillInt;
      NumInts -= 4;
    }
    //
    // Fill one int at a time
    //
    while (NumInts != 0) {
      *pInt++ = FillInt;
      NumInts--;
    }
    p = SEGGER_PTR2PTR(U8, pInt);                                                     // MISRA deviation D:100[e]
  }
  //
  // Fill the remainder byte wise
  //
  while (NumBytes != 0u) {
    *p++ = (U8)Fill;
    NumBytes--;
  }
  return pData;
}

/*********************************************************************
*
*       FS__CLIB_strncat
*
*  Function description
*    Appends n characters from the array
*    pointed to by s2 to the array pointed to by s1.
*
*  Parameters
*    s1     Pointer to a character array.
*    s2     Pointer to a character array.
*    n      Number of characters to append.
*
*  Return value
*    Value of s1.
*/
char * FS__CLIB_strncat(char * s1, const char * s2, U32 n)  {
  char * s;

  if (s1 != NULL) {
    for (s = s1; *s != '\0'; ++s) {               // Find end of s1[].
      ;
    }
    if (s2 != NULL) {
      for (; (0u < n) && (*s2 != '\0'); --n) {    // Copy at most n chars from s2[]
        *s++ = *s2++;
      }
      *s = '\0';
    }
  }
  return s1;
}

/*********************************************************************
*
*       FS__CLIB_strcmp
*
*  Function description
*    Compare the string pointed to by s1 with the
*    string pointed to by s2.
*
*  Parameters
*    s1     Pointer to a zero terminated string.
*    s2     Pointer to a zero terminated string.
*
*  Return value
*    ==0    Bytes are equal.
*    !=0    Bytes are different.
*/
int FS__CLIB_strcmp(const char * s1, const char * s2) {
  U8  c1;
  U8  c2;
  int r;

  do {
    c1 = (U8)*s1++;
    c2 = (U8)*s2++;
    r = (int)c1 - (int)c2;
    if (r != 0) {
      return r;             // Different
    }
  } while (c1 != 0u);
  return 0;                 // Equal
}

/*********************************************************************
*
*      FS__CLIB_strcpy
*
*  Function description
*    Copy characters from the array
*    pointed to by s2 to the array pointed to by s1.
*
*  Parameters
*    s1     Pointer to a character array.
*    s2     Pointer to a character array.
*
*  Return value
*    Value of s1.
*/
char * FS__CLIB_strcpy(char * s1, const char * s2) {
   char * p = NULL;

   if (s1 != NULL) {
     p = s1;
     while(*s2 != '\0') {
       *s1++ = *s2++;
     }
     *s1 = '\0';
   }
   return p;
}

/*********************************************************************
*
*       FS__CLIB_strlen
*
*  Function description
*    Compute the length of a string pointed to by s.
*
*  Parameters
*    s    Pointer to a zero terminated string.
*
*  Return value
*    Number of characters preceding the terminating 0.
*/
unsigned FS__CLIB_strlen(const char * s) {
  unsigned Len;

  Len = 0;
  while (*s++ != '\0') {
    Len++;
  }
  return Len;
}

/*********************************************************************
*
*       FS__CLIB_strncmp
*
*  Function description
*    Compare no more than n characters from the
*    array pointed to by s1 to the array pointed to by s2.
*
*  Parameters
*    s1     Pointer to a character array.
*    s2     Pointer to a character array.
*    n      Number of characters to compare.
*
*  Return value
*    ==0    Bytes are equal.
*    !=0    Bytes are different.
*/
int FS__CLIB_strncmp(const char * s1, const char * s2, unsigned n) {
  for (; n > 0u; n--) {
    U8  c1;
    U8  c2;
    int r;

    c1 = (U8)*s1++;
    c2 = (U8)*s2++;
    r  = (int)c1 - (int)c2;
    if (r != 0) {
      return r;             // Different
    }
    if (c1 == 0u) {
      return 0;             // Equal
    }
  }
  return 0;                 // Equal
}

/*********************************************************************
*
*       FS__CLIB_strncpy
*
*  Function description
*    Copy not more than n characters from the array
*    pointed to by s2 to the array pointed to by s1.
*
*  Parameters
*    s1     Pointer to a character array.
*    s2     Pointer to a character array.
*    n      Number of characters to copy.
*
*  Return value
*    Value of s1.
*/
char *FS__CLIB_strncpy(char * s1, const char * s2, U32 n) {
  char * s;

  s = s1;
  for (; (0u < n) && (*s2 != '\0'); --n) {
    *s++ = *s2++;         // copy at most n chars from s2[]
  }
  for (; 0u < n; --n) {
    *s++ = '\0';
  }
  return s1;
}

/*********************************************************************
*
*       FS__CLIB_strchr
*
*  Function description
*    Find the specified character in the string and returns a pointer to it.
*
*  Parameters
*    s      Pointer to a character array.
*    c      Character to be found.
*
*  Return value
*    Pointer in s to the found character.
*/
char * FS__CLIB_strchr(const char * s, int c) {
  for (;;) {
    if (*s == '\0') {
      return NULL;
    }
    if (*s == (char)c) {
      return (char *)s;       //lint !e9005 attempt to cast away const/volatile from a pointer or reference [MISRA 2012 Rule 11.8, required] D:105. Rationale: design error of the strchr() C standard library function.
    }
    s++;
  }
}

/*********************************************************************
*
*       FS__CLIB_toupper
*
*   Function description
*     Converts a small letter to the corresponding capital letter.
*
*   Parameters
*     c     Letter to convert.
*
*   Return value
*     Corresponding upper-case letter.
*/
int FS__CLIB_toupper(int c) {
  if ((c >= (int)'a') && (c <= (int)'z')) {
    c -= UPPER_CASE_OFF;
  }
  return c;
}

/*********************************************************************
*
*       FS__CLIB_tolower
*
*  Function description
*    Converts an capital letter to the corresponding small letter.
*
*  Parameters
*    c     Letter to convert.
*
*  Return value
*    Corresponding lower-case letter.
*/
int FS__CLIB_tolower(int c) {
  if ((c >= (int)'A') && (c <= (int)'Z')) {
    c += UPPER_CASE_OFF;
  }
  return c;
}

/*********************************************************************
*
*       FS__CLIB_isupper
*
*  Function description
*    Checks if a character is a capital letter.
*
*  Parameters
*    c      Character to check.
*
*  Return value
*    !=0    Character is upper-case letter
*    ==0    Character is not a upper-case letter
*/
int FS__CLIB_isupper(int c) {
  if ((c >= (int)'A') && (c <= (int)'Z')) {
    return 1;   // Character is an upper-case letter.
  }
  return 0;     // Character is not an upper-case letter.
}

/*********************************************************************
*
*       FS__CLIB_islower
*
*  Function description
*    Checks if a character is a small letter.
*
*  Parameters
*    c      Character to check.
*
*  Return value
*    !=0    Character is lower-case letter
*    ==0    Character is not a lower-case letter
*/
int FS__CLIB_islower(int c) {
  if ((c >= (int)'a') && (c <= (int)'z')) {
    return 1;   // Character is a lower-case letter.
  }
  return 0;     // Character is not a lower-case letter.
}

/*********************************************************************
*
*       FS_CLIB_Validate
*
*  Function description
*    Verifies the implementation of the replacement functions.
*
*  Return value
*    !=0    Error, one of the functions is defective.
*    ==0    OK, all the functions work correctly.
*
*  Notes
*    (1) From pubs.opengroup.org/onlinepubs/7908799/xsh/strncpy.html
*        "If the array pointed to by s2 is a string that is shorter than n bytes,
*         null bytes are appended to the copy in the array pointed to by s1, until
*         n bytes in all are written."
*
*    (2) Our character manipulation routines cannot handle extended ASCII characters.
*/
int FS_CLIB_Validate(void) {
  unsigned   i;
  unsigned   j;
  unsigned   k;
  int        r;
  int        rToCheck;
  U32        aData[5];
  U32        aDataToCheck[SEGGER_COUNTOF(aData)];
  U8       * pData8;
  U8       * pData8ToCheck;
  void     * p;
  void     * pToCheck;
  unsigned   NumBytes;
  const U8   abData1[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17};
  const U8   abData2[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 13, 14, 15, 16, 17, 18};
  char       acData[23];
  char       acDataToCheck[sizeof(acData)];
  char       acDataToCopy[sizeof(acData)];
  unsigned   Len;
  unsigned   LenToCheck;
  unsigned   LenToCopy;
  int        v;
  char     * pData;
  char     * pDataToCheck;

  //
  // Test memcmp()
  //
  for (i = 0; i < SEGGER_COUNTOF(abData1); ++i) {
    r        = FS__CLIB_memcmp(abData1, abData2, i);
    rToCheck = memcmp(abData1, abData2, i);
    if (r != rToCheck) {
      return 1;                                                         // Error
    }
  }
  for (i = 0; i < SEGGER_COUNTOF(abData1); ++i) {
    r        = FS__CLIB_memcmp(abData2, abData1, i);
    rToCheck = memcmp(abData2, abData1, i);
    if (r != rToCheck) {
      return 1;                                                         // Error
    }
  }
  //
  // Test memset()
  //
  pData8        = SEGGER_PTR2PTR(U8, aData);                            // MISRA deviation D:100[e]
  pData8ToCheck = SEGGER_PTR2PTR(U8, aDataToCheck);                     // MISRA deviation D:100[e]
  for (i = 0; i < sizeof(aData); ++i) {
    pData8[i]        = (U8)i;
    pData8ToCheck[i] = (U8)i;
  }
  NumBytes = sizeof(aData) - 4u;                                        // We need room to test unaligned addresses.
  for (j = 0; j < 3u; ++j) {
    for (i = 0; i < NumBytes; ++i) {
      p        = FS__CLIB_memset(pData8, (int)i, NumBytes);
      pToCheck = memset(pData8ToCheck, (int)i, NumBytes);
      if (pToCheck != pData8ToCheck) {
        return 1;                                                       // Error
      }
      if (p != pData8) {
        return 1;                                                       // Error
      }
      if (memcmp(pData8, pData8ToCheck, NumBytes) != 0) {
        return 1;                                                       // Error
      }
    }
    ++pData8;
    ++pData8ToCheck;
  }
  //
  // Test strcmp()
  //
  (void)FS__CLIB_memset(acData, 0, sizeof(acData));
  (void)FS__CLIB_memset(acDataToCheck, 0, sizeof(acDataToCheck));
  r        = FS__CLIB_strcmp(acData, acDataToCheck);
  rToCheck = strcmp(acData, acDataToCheck);
  if (r != 0) {
    return 1;                                                           // Error
  }
  if (r != rToCheck) {
    return 1;                                                           // Error
  }
  for (i = 0; i < (sizeof(acData) - 1u); ++i) {
    v = (int)'a' + (int)i;
    acData[i] = (char)v;
    r        = FS__CLIB_strcmp(acData, acDataToCheck);
    rToCheck = strcmp(acData, acDataToCheck);
    if (r != v) {
      return 1;                                                         // Error
    }
#if SIMULATOR_RUN
    if (rToCheck <= 0) {
      return 1;                                                         // Error
    }
#else
    if (r != rToCheck) {
      return 1;                                                         // Error
    }
#endif
    acDataToCheck[i] = acData[i];
  }
  (void)FS__CLIB_memset(acData, 0, sizeof(acData));
  (void)FS__CLIB_memset(acDataToCheck, 0, sizeof(acDataToCheck));
  for (i = 0; i < (sizeof(acDataToCheck) - 1u); ++i) {
    v = (int)'a' + (int)i;
    acDataToCheck[i] = (char)v;
    r        = FS__CLIB_strcmp(acData, acDataToCheck);
    rToCheck = strcmp(acData, acDataToCheck);
    if (r != -v) {
      return 1;                                                         // Error
    }
#if SIMULATOR_RUN
    if (rToCheck >= 0) {
      return 1;                                                         // Error
    }
#else
    if (r != rToCheck) {
      return 1;                                                         // Error
    }
#endif
    acData[i] = acDataToCheck[i];
  }
  //
  // Test strlen()
  //
  (void)FS__CLIB_memset(acData, 0, sizeof(acData));
  Len        = FS__CLIB_strlen(acData);
  LenToCheck = (unsigned)strlen(acData);
  if (Len != 0u) {
    return 1;                                                           // Error
  }
  if (Len != LenToCheck) {
    return 1;                                                           // Error
  }
  for (i = 0; i < (sizeof(acData) - 1u); ++i) {
    acData[i] = 'a' + i;
    Len        = FS__CLIB_strlen(acData);
    LenToCheck = (unsigned)strlen(acData);
    if (Len != (i + 1u)) {
      return 1;                                                         // Error
    }
    if (Len != LenToCheck) {
      return 1;                                                         // Error
    }
  }
  //
  // Test strncmp()
  //
  (void)FS__CLIB_memset(acData, 0, sizeof(acData));
  (void)FS__CLIB_memset(acDataToCheck, 0, sizeof(acDataToCheck));
  r        = FS__CLIB_strncmp(acData, acDataToCheck, sizeof(acData));
  rToCheck = strncmp(acData, acDataToCheck, sizeof(acData));
  if (r != 0) {
    return 1;                                                           // Error
  }
  if (r != rToCheck) {
    return 1;                                                           // Error
  }
  for (i = 0; i < sizeof(acData); ++i) {
    v = (int)'a' + (int)i;
    acData[i] = (char)v;
    r        = FS__CLIB_strncmp(acData, acDataToCheck, sizeof(acData));
    rToCheck = strncmp(acData, acDataToCheck, sizeof(acData));
    if (r != v) {
      return 1;                                                         // Error
    }
#if SIMULATOR_RUN
    if (rToCheck <= 0) {
      return 1;                                                         // Error
    }
#else
    if (r != rToCheck) {
      return 1;                                                         // Error
    }
#endif
    acDataToCheck[i] = acData[i];
  }
  (void)FS__CLIB_memset(acData, 0, sizeof(acData));
  (void)FS__CLIB_memset(acDataToCheck, 0, sizeof(acDataToCheck));
  for (i = 0; i < sizeof(acDataToCheck); ++i) {
    v = (int)'a' + (int)i;
    acDataToCheck[i] = (char)v;
    r        = FS__CLIB_strncmp(acData, acDataToCheck, sizeof(acDataToCheck));
    rToCheck = strncmp(acData, acDataToCheck, sizeof(acDataToCheck));
    if (r != -v) {
      return 1;                                                         // Error
    }
#if SIMULATOR_RUN
    if (rToCheck >= 0) {
      return 1;                                                         // Error
    }
#else
    if (r != rToCheck) {
      return 1;                                                         // Error
    }
#endif
    acData[i] = acDataToCheck[i];
  }
#if SIMULATOR_RUN   // Run this test only on simulator because gcc complains about truncated strings.
  //
  // Test strncpy()
  //
  (void)FS__CLIB_memset(acData, 0x7F, sizeof(acData));
  (void)FS__CLIB_memset(acDataToCheck, 0x7F, sizeof(acDataToCheck));
  (void)FS__CLIB_memset(acDataToCopy, 0, sizeof(acDataToCopy));
  pData        = FS__CLIB_strncpy(acData, acDataToCopy, 0);
  pDataToCheck = strncpy(acDataToCheck, acDataToCopy, 0);
  if (pData != acData) {
    return 1;                                                           // Error
  }
  for (i = 0; i < sizeof(acData); ++i) {
    if (acData[i] != '\x7F') {
      return 1;                                                         // Error
    }
  }
  if (pDataToCheck != acDataToCheck) {
    return 1;                                                           // Error
  }
  for (i = 0; i < sizeof(acDataToCheck); ++i) {
    if (acDataToCheck[i] != '\x7F') {                                   //lint !e690: Possible access of pointer past nul character by operator '[' N:107. Rationale: we purposely overwrite the 0-terminator.
      return 1;                                                         // Error
    }
  }
  for (i = 1; i < sizeof(acDataToCopy); ++i) {
    v = (int)'a' + (int)i - 1;
    (void)FS__CLIB_memset(acDataToCopy, v, i);
    LenToCopy = FS__CLIB_strlen(acDataToCopy);
    if (LenToCopy != i) {
      return 1;                                                         // Error
    }
    for (j = 1; j < sizeof(acDataToCopy); ++j) {
      (void)FS__CLIB_memset(acData, 0x7F, sizeof(acData));
      (void)FS__CLIB_memset(acDataToCheck, 0x7F, sizeof(acDataToCheck));
      pData        = FS__CLIB_strncpy(acData, acDataToCopy, j);
      pDataToCheck = strncpy(acDataToCheck, acDataToCopy, j);
      if (j > i) {
        Len          = FS__CLIB_strlen(acData);
        LenToCheck   = FS__CLIB_strlen(acDataToCheck);
        if (Len != LenToCopy) {
          return 1;                                                     // Error
        }
        if (LenToCheck != LenToCopy) {
          return 1;                                                     // Error
        }
      }
      if (pData != acData) {
        return 1;                                                       // Error
      }
      for (k = 0; k < sizeof(acData); ++k) {
        if (j <= i) {
          if (k < j) {
            if (acData[k] != (char)v) {
              return 1;                                                 // Error
            }
          } else {
            if (acData[k] != '\x7F') {
              return 1;                                                 // Error
            }
          }
        } else {
          if (k < Len) {
            if (acData[k] != (char)v) {
              return 1;                                                 // Error
            }
          } else {
            if ((k >= Len) && (k < j)) {                                // See Note 1 for why we check if k < j
              if (acData[k] != '\0') {
                return 1;                                               // Error
              }
            } else {
              if (acData[k] != '\x7F') {
                return 1;                                               // Error
              }
            }
          }
        }
      }
      if (pDataToCheck != acDataToCheck) {
        return 1;                                                       // Error
      }
      for (k = 0; k < sizeof(acDataToCheck); ++k) {
        if (j <= i) {
          if (k < j) {
            if (acDataToCheck[k] != (char)v) {
              return 1;                                                 // Error
            }
          } else {
            if (acDataToCheck[k] != '\x7F') {
              return 1;                                                 // Error
            }
          }
        } else {
          if (k < LenToCheck) {
            if (acDataToCheck[k] != (char)v) {
              return 1;                                                 // Error
            }
          } else {
            if ((k >= LenToCheck) && (k < j)) {                         // See Note 1 for why we check if k < j
              if (acDataToCheck[k] != '\0') {
                return 1;                                               // Error
              }
            } else {
              if (acDataToCheck[k] != '\x7F') {
                return 1;                                               // Error
              }
            }
          }
        }
      }
    }
  }
#endif // SIMULATOR_RUN
  //
  // Test strchr()
  //
  (void)FS__CLIB_memset(acData, 0, sizeof(acData));
  pData        = FS__CLIB_strchr(acData, (int)'a');
  pDataToCheck = strchr(acData, (int)'a');
  if (pData != NULL) {
    return 1;                                                           // Error
  }
  if (pDataToCheck != NULL) {
    return 1;                                                           // Error
  }
  (void)FS__CLIB_memset(acData, (int)'a', sizeof(acData) - 1u);
  for (i = 0; i < (sizeof(acData) - 1u); ++i) {
    pData        = FS__CLIB_strchr(acData, (int)'a');
    pDataToCheck = strchr(acData, (int)'a');
    if (pData != &acData[i]) {
      return 1;                                                         // Error
    }
    if (pDataToCheck != &acData[i]) {
      return 1;                                                         // Error
    }
    acData[i] = 'b';
  }
  //
  // Test toupper()
  //
  for (i = 0; i < 128u; ++i) {                                          // See Note 2 for why we check only up to 128
    r        = FS__CLIB_toupper((int)i);
    rToCheck = toupper((int)i);
    if (r != rToCheck) {
      return 1;                                                         // Error
    }
  }
  //
  // Test tolower()
  //
  for (i = 0; i < 128u; ++i) {                                          // See Note 2 for why we check only up to 128
    r        = FS__CLIB_tolower((int)i);
    rToCheck = tolower((int)i);
    if (r != rToCheck) {
      return 1;                                                         // Error
    }
  }
  //
  // Test isupper()
  //
  for (i = 0; i < 128u; ++i) {                                          // See Note 2 for why we check only up to 128
    r        = FS__CLIB_isupper((int)i);
    rToCheck = isupper((int)i);                                         // Linux returns 256 in case of a capital letter.
    if ((r != 0) && (rToCheck == 0)) {
      return 1;                                                         // Error
    }
    if ((r == 0) && (rToCheck != 0)) {
      return 1;                                                         // Error
    }
  }
  //
  // Test islower()
  //
  for (i = 0; i < 128u; ++i) {                                          // See Note 2 for why we check only up to 128
    r        = FS__CLIB_islower((int)i);
    rToCheck = islower((int)i);                                         // Windows returns 2 in case of a small letter.
    if ((r != 0) && (rToCheck == 0)) {
      return 1;                                                         // Error
    }
    if ((r == 0) && (rToCheck != 0)) {
      return 1;                                                         // Error
    }
  }
  //
  // Test strcpy()
  //
  (void)FS__CLIB_memset(acData, 0x7F, sizeof(acData));
  (void)FS__CLIB_memset(acDataToCheck, 0x7F, sizeof(acDataToCheck));
  (void)FS__CLIB_memset(acDataToCopy, 0, sizeof(acDataToCopy));
  pData        = FS__CLIB_strcpy(acData, acDataToCopy);
  pDataToCheck = strcpy(acDataToCheck, acDataToCopy);                   // NOLINT
  if (pData != acData) {
    return 1;                                                           // Error
  }
  if (acData[0] != '\0') {
    return 1;                                                           // Error
  }
  for (i = 1; i < sizeof(acData); ++i) {
    if (acData[i] != '\x7F') {                                          //lint !e690: Possible access of pointer past nul character by operator '[' N:107. Rationale: we purposely overwrite the 0-terminator.
      return 1;                                                         // Error
    }
  }
  if (pDataToCheck != acDataToCheck) {
    return 1;                                                           // Error
  }
  if (acDataToCheck[0] != '\0') {
    return 1;                                                           // Error
  }
  for (i = 1; i < sizeof(acDataToCheck); ++i) {
    if (acDataToCheck[i] != '\x7F') {                                   //lint !e690: Possible access of pointer past nul character by operator '[' N:107. Rationale: we purposely overwrite the 0-terminator.
      return 1;                                                         // Error
    }
  }
  for (i = 1; i < sizeof(acDataToCopy); ++i) {
    v = (int)'a' + (int)i - 1;
    (void)FS__CLIB_memset(acDataToCopy, v, i);
    LenToCopy = FS__CLIB_strlen(acDataToCopy);
    if (LenToCopy != i) {
      return 1;                                                         // Error
    }
    (void)FS__CLIB_memset(acData, 0x7F, sizeof(acData));
    (void)FS__CLIB_memset(acDataToCheck, 0x7F, sizeof(acDataToCheck));
    pData        = FS__CLIB_strcpy(acData, acDataToCopy);
    pDataToCheck = strcpy(acDataToCheck, acDataToCopy);                 // NOLINT
    Len          = FS__CLIB_strlen(acData);
    LenToCheck   = FS__CLIB_strlen(acDataToCheck);
    if (Len != LenToCopy) {
      return 1;                                                         // Error
    }
    if (LenToCheck != LenToCopy) {
      return 1;                                                         // Error
    }
    if (pData != acData) {
      return 1;                                                         // Error
    }
    for (k = 0; k < sizeof(acData); ++k) {
      if (k < Len) {
        if (acData[k] != (char)v) {
          return 1;                                                     // Error
        }
      } else {
        if (k == Len) {
          if (acData[k] != '\0') {
            return 1;                                                   // Error
          }
        } else {
          if (acData[k] != '\x7F') {
            return 1;                                                   // Error
          }
        }
      }
    }
    if (pDataToCheck != acDataToCheck) {
      return 1;                                                         // Error
    }
    for (k = 0; k < sizeof(acDataToCheck); ++k) {
      if (k < LenToCheck) {
        if (acDataToCheck[k] != (char)v) {
          return 1;                                                     // Error
        }
      } else {
        if (k == LenToCheck) {
          if (acDataToCheck[k] != '\0') {
            return 1;                                                   // Error
          }
        } else {
          if (acDataToCheck[k] != '\x7F') {
            return 1;                                                   // Error
          }
        }
      }
    }
  }
#if SIMULATOR_RUN   // Run this test only on simulator because gcc complains about truncated strings.
  //
  // Test strncat()
  //
  (void)FS__CLIB_memset(acData, 0x7F, sizeof(acData));
  (void)FS__CLIB_memset(acDataToCheck, 0x7F, sizeof(acDataToCheck));
  (void)FS__CLIB_memset(acDataToCopy, 0, sizeof(acDataToCopy));
  acData[0]        = '\0';
  acDataToCheck[0] = '\0';
  pData        = FS__CLIB_strncat(acData, acDataToCopy, 0);
  pDataToCheck = strncat(acDataToCheck, acDataToCopy, 0);
  if (pData != acData) {
    return 1;                                                           // Error
  }
  if (acData[0] != '\0') {
    return 1;                                                           // Error
  }
  for (i = 1; i < sizeof(acData); ++i) {
    if (acData[i] != '\x7F') {                                          //lint !e690: Possible access of pointer past nul character by operator '[' N:107. Rationale: we purposely overwrite the 0-terminator.
      return 1;                                                         // Error
    }
  }
  if (pDataToCheck != acDataToCheck) {
    return 1;                                                           // Error
  }
  if (acDataToCheck[0] != '\0') {
    return 1;                                                           // Error
  }
  for (i = 1; i < sizeof(acDataToCheck); ++i) {
    if (acDataToCheck[i] != '\x7F') {                                   //lint !e690: Possible access of pointer past nul character by operator '[' N:107. Rationale: we purposely overwrite the 0-terminator.
      return 1;                                                         // Error
    }
  }
  for (i = 1; i < sizeof(acDataToCopy); ++i) {
    for (j = 0; j < (sizeof(acDataToCopy) - i); ++j) {
      (void)FS__CLIB_memset(acData, 0x7F, sizeof(acData));
      (void)FS__CLIB_memset(acDataToCheck, 0x7F, sizeof(acDataToCheck));
      v = (int)'a';
      for (k = 0; k < i; ++k) {
        acData[k]        = (char)v;
        acDataToCheck[k] = (char)v;
        ++v;
      }
      acData[k]        = '\0';
      acDataToCheck[k] = '\0';
      for (k = 0; k <= j; ++k) {
        acDataToCopy[k] = (char)v + (int)k;
      }
      acDataToCopy[k] = '\0';
      LenToCopy  = FS__CLIB_strlen(acDataToCopy);
      Len        = FS__CLIB_strlen(acData);
      LenToCheck = FS__CLIB_strlen(acDataToCheck);
      for (k = 1; k < (sizeof(acDataToCopy) - i); ++k) {
        unsigned LenNewToCopy;
        unsigned LenNew;
        unsigned LenNewToCheck;
        unsigned l;

        pData         = FS__CLIB_strncat(acData, acDataToCopy, k);
        pDataToCheck  = strncat(acDataToCheck, acDataToCopy, k);
        LenNew        = FS__CLIB_strlen(acData);
        LenNewToCheck = FS__CLIB_strlen(acDataToCheck);
        LenNewToCopy  = SEGGER_MIN(LenToCopy, k);
        if ((Len + LenNewToCopy) != LenNew) {
          return 1;                                                     // Error
        }
        if ((LenToCheck + LenNewToCopy) != LenNewToCheck) {
          return 1;                                                     // Error
        }
        if (LenNew != LenNewToCheck) {
          return 1;                                                     // Error
        }
        if (pData != acData) {
          return 1;                                                     // Error
        }
        for (l = 0; l < sizeof(acData); ++l) {
          if (l < LenNew) {
            v = (int)'a' + (int)l;
            if (acData[l] != (char)v) {
              return 1;                                                 // Error
            }
          } else {
            if (l == LenNew) {
              if (acData[l] != '\0') {
                return 1;                                               // Error
              }
            } else {
              if (acData[l] != '\x7F') {
                return 1;                                               // Error
              }
            }
          }
        }
        if (pDataToCheck != acDataToCheck) {
          return 1;                                                     // Error
        }
        for (l = 0; l < sizeof(acDataToCheck); ++l) {
          if (l < LenNewToCheck) {
            v = (int)'a' + (int)l;
            if (acDataToCheck[l] != (char)v) {
              return 1;                                                 // Error
            }
          } else {
            if (l == LenNewToCheck) {
              if (acDataToCheck[l] != '\0') {
                return 1;                                               // Error
              }
            } else {
              if (acDataToCheck[l] != '\x7F') {
                return 1;                                               // Error
              }
            }
          }
        }
        acData[i]        = '\0';
        acDataToCheck[i] = '\0';
      }
    }
  }
#endif // SIMULATOR_RUN
  return 0;                                                             // OK, all tests passed.
}

/*************************** End of file ****************************/
