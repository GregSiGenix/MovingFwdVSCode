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
File        : FS_CACHE_Core.c
Purpose     : Implementation of sector cache API functions.
-------------------------- END-OF-HEADER -----------------------------
*/

/*********************************************************************
*
*       #include Section
*
**********************************************************************
*/
#include "FS_Int.h"

#if FS_SUPPORT_CACHE

/*********************************************************************
*
*       Public code (internal)
*
**********************************************************************
*/

/*********************************************************************
*
*       FS__CACHE_CommandDeviceNL
*
*  Function description
*    Function that sends a command to a cache module, if attached to the specific volume.
*    This function does not lock.
*
*  Return value
*    ==0    OK, command executed.
*    < 0    An error occurred.
*/
int FS__CACHE_CommandDeviceNL(FS_DEVICE * pDevice, int Cmd, void * pData) {
  int                  r;
  const FS_CACHE_API * pCacheAPI;

  r = 0;                  // Set to indicate success.
  pCacheAPI = pDevice->Data.pCacheAPI;
  if (pCacheAPI != NULL) {
    if (pCacheAPI->pfCommand != NULL) {
      r = pCacheAPI->pfCommand(pDevice, Cmd, pData);
    }
  }
  return r;
}

/*********************************************************************
*
*       FS__CACHE_CommandDevice
*
*  Function description
*    Function that sends a command to a cache module, if attached to the specific volume.
*    This function does a driver lock and simply calls the non-locking function.
*
*  Return value
*    ==0    OK, command executed.
*    < 0    An error occurred.
*/
int FS__CACHE_CommandDevice(FS_DEVICE * pDevice, int Cmd, void * pData) {
  int r;

  FS_LOCK_DRIVER(pDevice);
  r = FS__CACHE_CommandDeviceNL(pDevice, Cmd, pData);
  FS_UNLOCK_DRIVER(pDevice);
  return r;
}

/*********************************************************************
*
*       FS__CACHE_CommandVolumeNL
*
*  Function description
*    Non-locking variant of FS__CACHE_CommandVolume()
*    Function that sends a command to a cache module, if attached to the specific volume.
*    This function extracts the device pointer from pVolume and calls the
*    FS__CACHE_CommandDeviceNL() function.
*
*  Return value
*    ==0    OK, command executed.
*    < 0    An error occurred.
*/
int FS__CACHE_CommandVolumeNL(FS_VOLUME * pVolume, int Cmd, void * pData) {
  int r;

  r = -1;
  if (pVolume != NULL) {
    FS_DEVICE * pDevice;

    pDevice = &pVolume->Partition.Device;
    r = FS__CACHE_CommandDeviceNL(pDevice, Cmd, pData);
  }
  return r;
}


/*********************************************************************
*
*       FS__CACHE_CommandVolume
*
*  Function description
*    Function that sends a command to a cache module, if attached to the specific volume.
*    This function extracts the device pointer from pVolume and calls the
*    FS__CACHE_CommandDevice.
*
*  Return value
*    ==0    OK, command executed.
*    < 0    An error occurred.
*/
int FS__CACHE_CommandVolume(FS_VOLUME * pVolume, int Cmd, void * pData) {
  int r;

  r = -1;
  if (pVolume != NULL) {
    FS_DEVICE * pDevice;

    pDevice = &pVolume->Partition.Device;
    r = FS__CACHE_CommandDevice(pDevice, Cmd, pData);
  }
  return r;
}

/*********************************************************************
*
*       FS__CACHE_CleanNL
*
*  Function description
*    Writes dirty sector cache entries to storage (non-locking version).
*
*  Parameters
*    pDevice    Storage device on which the cache should be cleaned.
*
*  Return value
*    ==0      OK, sector cache cleaned.
*    !=0      An error occurred.
*/
int FS__CACHE_CleanNL(FS_DEVICE * pDevice) {
  int r;

  r = FS__CACHE_CommandDeviceNL(pDevice,  FS_CMD_CACHE_CLEAN, NULL);
  return r;
}

/*********************************************************************
*
*       FS__CACHE_Clean
*
*  Function description
*    Writes dirty sector cache entries to storage.
*
*  Parameters
*    pVolume    Volume on which the cache should be cleaned.
*
*  Return value
*    ==0      OK, sector cache cleaned.
*    !=0      An error occurred.
*/
int FS__CACHE_Clean(FS_VOLUME * pVolume) {
  FS_DEVICE * pDevice;
  int         r;

  pDevice = &pVolume->Partition.Device;
  FS_LOCK_DRIVER(pDevice);
  r = FS__CACHE_CleanNL(pDevice);
  FS_UNLOCK_DRIVER(pDevice);
  return r;
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       FS_AssignCache
*
*  Function description
*    Enables / disables a sector cache for a specific volume.
*
*  Parameters
*    sVolumeName    Name of the volume on which the sector cache should
*                   be enabled / disabled. Cannot be NULL.
*    pData          Pointer to a memory area that should be used as cache.
*    NumBytes       Number of bytes in the memory area pointed by pData.
*    Type           Type of the sector cache to be configured.
*                   It can take one of the following values:
*                   * FS_CACHE_ALL
*                   * FS_CACHE_MAN
*                   * FS_CACHE_RW
*                   * FS_CACHE_RW_QUOTA
*                   * FS_CACHE_MULTI_WAY.
*
*  Return value
*    > 0  Number of sectors which fit in cache.
*    ==0  An error occurred. The memory area cannot be used as sector cache.
*
*  Additional information
*    The first configured volume is used if the empty string is specified
*    as sVolumeName.
*
*    To disable the cache for a specific device, call FS_AssignCache()
*    with NumBytes set to 0. In this case the function returns 0.
*
*    A range of the memory block assigned to the sector cache is used
*    to store the management data. The following defines can help an
*    application allocate a memory block sufficiently large to store
*    a specified number of logical sectors: FS_SIZEOF_CACHE_ALL(),
*    FS_SIZEOF_CACHE_MAN(), FS_SIZEOF_CACHE_RW(), FS_SIZEOF_CACHE_RW_QUOTA(),
*    or FS_SIZEOF_CACHE_MULTI_WAY().
*/
U32 FS_AssignCache(const char * sVolumeName, void * pData, I32 NumBytes, FS_CACHE_TYPE Type) {
  U32             r;
  FS_VOLUME     * pVolume;
  FS_DEVICE     * pDevice;
  FS_CACHE_INIT * pfInit;

  FS_LOCK();
  r = 0;
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    pDevice = &pVolume->Partition.Device;
    FS_LOCK_DRIVER(pDevice);
    if (pDevice->Data.pCacheAPI != NULL) {
      //
      // Use non-locking variant of FS__CACHE_CommandVolume(), since the calling function, FS_AssignCache(), already locks
      // If an OS which does not allow multiple locking out of the same task, is used, this can cause a dead-lock.
      //
      (void)FS__CACHE_CommandVolumeNL(pVolume, FS_CMD_CACHE_CLEAN, NULL);
    }
    pfInit = Type;
    if (NumBytes == 0) {
      pfInit = NULL;
    }
    if (pData == NULL) {
      pfInit = NULL;
    }
    if (pfInit != NULL) {
      r = (*pfInit)(pDevice, pData, NumBytes);
    } else {
      pDevice->Data.pCacheAPI  = NULL;
      pDevice->Data.pCacheData = NULL;
      r = 0;
    }
    FS_UNLOCK_DRIVER(pDevice);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_CACHE_SetMode
*
*  Function description
*    Sets the operating mode of sector cache.
*
*  Parameters
*    sVolumeName    Name of the volume for which the cache should be
*                   cleaned. If not specified, the first volume will
*                   be used.
*    TypeMask       Specifies the types of the sectors that have to
*                   be cached. This parameter can be an OR-combination
*                   of the Sector type masks (FS_SECTOR_TYPE_MASK_...)
*    ModeMask       Specifies the operating mode of the cache module.
*                   This parameter can be one of the values defined
*                   as Sector cache modes (FS_CACHE_MODE_...)
*
*  Return value
*    ==0      OK, mode set.
*    !=0      An error occurred.
*
*  Additional information
*    This  function is supported by the following cache types: FS_CACHE_RW,
*    FS_CACHE_RW_QUOTA, and FS_CACHE_MULTI_WAY. These cache modules
*    have to be configured using this function otherwise, neither
*    read nor write operations are cached.
*
*    When configured in FS_CACHE_MODE_WB mode the cache module writes
*    the sector data automatically to storage device if free space
*    is required for new sector data. The application can call the
*    FS_CACHE_Clean() function at any time to write all the cache
*    sector data to storage device.
*/
int FS_CACHE_SetMode(const char * sVolumeName, int TypeMask, int ModeMask) {
  int          r;
  FS_VOLUME  * pVolume;
  CACHE_MODE   CacheMode;

  FS_LOCK();
  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    CacheMode.TypeMask = TypeMask;
    CacheMode.ModeMask = ModeMask;
    r = FS__CACHE_CommandVolume(pVolume, FS_CMD_CACHE_SET_MODE, &CacheMode);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_CACHE_SetQuota
*
*  Function description
*    Sets the quotas of a specific sector cache.
*
*  Parameters
*    sVolumeName    Name of the volume for which the cache should be
*                   cleaned. If not specified, the first volume will
*                   be used.
*    TypeMask       Specifies the types of the sectors that have to
*                   be cached. This parameter can be an OR-combination
*                   of the Sector type masks (FS_SECTOR_TYPE_MASK_...)
*    NumSectors     Specifies the maximum number of sectors to be stored
*                   for each sector type specified in TypeMask.
*
*  Return value
*    ==0      OK, maximum number of sectors set.
*    !=0      An error occurred.
*
*  Additional information
*    This function is currently only usable with the FS_CACHE_RW_QUOTA
*    cache module. After the FS_CACHE_RW_QUOTA cache module has been
*    assigned to a volume and the cache mode has been set, the quotas
*    for the different sector types have to be configured using this
*    function. Otherwise, neither read nor write operations are cached.
*/
int FS_CACHE_SetQuota(const char * sVolumeName, int TypeMask, U32 NumSectors) {
  int           r;
  FS_VOLUME   * pVolume;
  CACHE_QUOTA   CacheQuota;

  FS_LOCK();
  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    CacheQuota.TypeMask   = TypeMask;
    CacheQuota.NumSectors = NumSectors;
    r = FS__CACHE_CommandVolume(pVolume, FS_CMD_CACHE_SET_QUOTA, &CacheQuota);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_CACHE_SetAssocLevel
*
*  Function description
*    Modifies the associativity level of multi-way cache.
*
*  Parameters
*    sVolumeName    Name of the volume for which the cache should be
*                   cleaned. If not specified, the first volume will
*                   be used.
*    AssocLevel     Number of entries in the cache. It has to be
*                   a power of two value.
*
*  Return value
*    ==0      OK, associativity level set.
*    !=0      An error occurred.
*
*  Additional information
*    This function is supported only by the FS_CACHE_MULTI_WAY cache module.
*    An error is returned if the function is used with any other cache module.
*
*    The associativity level specifies on how many different places in the
*    cache the data of the same sector can be stored. The cache replacement
*    policy uses this information to decide where to store the contents of
*    a sector in the cache. Caches with higher associativity levels tend to
*    have higher hit rates. The default associativity level is two.
*/
int FS_CACHE_SetAssocLevel(const char * sVolumeName, int AssocLevel) {
  int         r;
  FS_VOLUME * pVolume;
  U32         Data32;

  FS_LOCK();
  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    Data32 = (U32)AssocLevel;
    r = FS__CACHE_CommandVolume(pVolume, FS_CMD_CACHE_SET_ASSOC_LEVEL, &Data32);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_CACHE_GetNumSectors
*
*  Function description
*    Queries the size of the sector cache.
*
*  Parameters
*    sVolumeName    Name of the volume for which the cache should be
*                   cleaned. If not specified, the first volume will
*                   be used.
*    pNumSectors    [OUT] Number of sectors that can be stored
*                   in the cache at the same time. It cannot be NULL.
*
*  Return value
*    ==0      OK, number of sectors returned.
*    !=0      An error occurred.
*
*  Additional information
*    This function returns the number of sectors that can be stored
*    in the cache at the same time.
*/
int FS_CACHE_GetNumSectors(const char * sVolumeName, U32 * pNumSectors) {
  int         r;
  FS_VOLUME * pVolume;

  FS_LOCK();
  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    r = FS__CACHE_CommandVolume(pVolume, FS_CMD_CACHE_GET_NUM_SECTORS, pNumSectors);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_CACHE_Clean
*
*  Function description
*    Writes modified sector data to storage device.
*
*  Parameters
*    sVolumeName    Name of the volume for which the cache should be
*                   cleaned. If not specified, the first volume will
*                   be used.
*
*  Return value
*    ==0      Success, the cache has been emptied.
*    !=0      An error occurred.
*
*  Additional information
*    This function can be used to make sure that modifications made to
*    cached data are also committed to storage device.
*
*    Because only write or read / write caches need to be cleaned,
*    this function can only be called for volumes where FS_CACHE_RW,
*    FS_CACHE_RW_QUOTA, or FS_CACHE_MULTI_WAY module is assigned.
*    The other cache modules ignore the cache clean operation.
*
*    The cleaning of the cache is also performed when the volume
*    is unmounted via FS_Unmount() or when the cache is disabled
*    or reassigned via FS_AssignCache().
*/
int FS_CACHE_Clean(const char * sVolumeName) {
  FS_VOLUME * pVolume;
  int         r;

  r = FS_ERRCODE_VOLUME_NOT_FOUND;        // Set to indicate failure.
  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    r = FS__CACHE_Clean(pVolume);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_CACHE_Invalidate
*
*  Function description
*    Removes all sector data from cache.
*
*  Parameters
*    sVolumeName    Name of the volume for which the cache should be
*                   cleaned. If not specified, the first volume will
*                   be used.
*
*  Return value
*    ==0      Success, the cache has been emptied.
*    !=0      An error occurred.
*
*  Additional information
*    This function can be called to remove all the sectors from the
*    cache. FS_CACHE_Invalidate() does not write to storage modified
*    sector data. After calling FS_CACHE_Invalidate() the contents
*    of modified sector data is lost. FS_CACHE_Clean() has to be
*    called first to prevent a data loss.
*/
int FS_CACHE_Invalidate(const char * sVolumeName) {
  FS_VOLUME * pVolume;
  int         r;

  r = FS_ERRCODE_VOLUME_NOT_FOUND;        // Set to indicate failure.
  FS_LOCK();
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    r = FS__CACHE_CommandVolume(pVolume, FS_CMD_CACHE_INVALIDATE, pVolume->Partition.Device.Data.pCacheData);
  }
  FS_UNLOCK();
  return r;
}

/*********************************************************************
*
*       FS_CACHE_GetType
*
*  Function description
*    Queries the type of the configured sector cache.
*
*  Parameters
*    sVolumeName    Name of the volume for which the cache should be
*                   cleaned. If not specified, the first volume will
*                   be used.
*    pType          [OUT] Cache sector type.
*                   * FS_CACHE_NONE       No sector cache configured.
*                   * FS_CACHE_ALL        A pure read cache.
*                   * FS_CACHE_MAN        A pure read cache that caches only the management sectors.
*                   * FS_CACHE_RW         A read / write cache module.
*                   * FS_CACHE_RW_QUOTA   A read / write cache module with configurable capacity per sector type.
*                   * FS_CACHE_MULTI_WAY  A read / write cache module with configurable associativity level.
*
*  Return value
*    ==0      OK, type of sector cache returned.
*    !=0      An error occurred.
*/
int FS_CACHE_GetType(const char * sVolumeName, FS_CACHE_TYPE * pType) {
  int         r;
  FS_VOLUME * pVolume;

  FS_LOCK();
  r = FS_ERRCODE_VOLUME_NOT_FOUND;
  pVolume = FS__FindVolume(sVolumeName);
  if (pVolume != NULL) {
    r = FS__CACHE_CommandVolume(pVolume, FS_CMD_CACHE_GET_TYPE, pType);
  }
  FS_UNLOCK();
  return r;
}

#endif // FS_SUPPORT_CACHE

/*************************** End of file ****************************/
