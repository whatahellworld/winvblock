/**
 * Copyright (C) 2009-2011, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
 * Copyright 2006-2008, V.
 * For WinAoE contact information, see http://winaoe.org/
 *
 * This file is part of WinVBlock, derived from WinAoE.
 *
 * WinVBlock is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinVBlock is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with WinVBlock.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * Disk device specifics.
 */

#include <ntddk.h>

#include "portable.h"
#include "winvblock.h"
#include "wv_stdlib.h"
#include "irp.h"
#include "driver.h"
#include "bus.h"
#include "device.h"
#include "disk.h"
#include "debug.h"

#ifndef _MSC_VER
static long long __divdi3(long long u, long long v) {
    return u / v;
  }
#endif

/** Exports. */
WVL_M_LIB BOOLEAN WvlDiskIsRemovable[WvlDiskMediaTypes] = {TRUE, FALSE, TRUE};
WVL_M_LIB PWCHAR WvlDiskCompatIds[WvlDiskMediaTypes] = {
    L"GenSFloppy",
    L"GenDisk",
    L"GenCdRom"
  };

/* Fetch a disk's unit number.  Defaults to 0. */
WVL_M_LIB UCHAR STDCALL WvlDiskUnitNum(IN WV_SP_DISK_T Disk) {
    return Disk->disk_ops.UnitNum ? Disk->disk_ops.UnitNum(Disk) : 0;
  }

/* Handle an IRP_MJ_POWER IRP. */
WVL_M_LIB NTSTATUS STDCALL WvlDiskPower(
    IN PDEVICE_OBJECT DevObj,
    IN PIRP Irp,
    IN WV_SP_DISK_T Disk
  ) {
    PoStartNextPowerIrp(Irp);
    return WvlIrpComplete(Irp, 0, STATUS_NOT_SUPPORTED);
  }

/* Handle an IRP_MJ_SYSTEM_CONTROL IRP. */
WVL_M_LIB NTSTATUS STDCALL WvlDiskSysCtl(
    IN PDEVICE_OBJECT DevObj,
    IN PIRP Irp,
    IN WV_SP_DISK_T Disk
  ) {
    return WvlIrpComplete(Irp, 0, Irp->IoStatus.Status);
  }

/**
 * Create a disk PDO.
 *
 * @v DriverObj         The driver to associate with the PDO.
 * @v ExtensionSize     The device object extension size.
 * @v MediaType         The media type for the device.
 * @v Pdo               Points to the PDO pointer to fill, upon success.
 */
WVL_M_LIB NTSTATUS STDCALL WvlDiskCreatePdo(
    IN PDRIVER_OBJECT DriverObj,
    IN SIZE_T ExtensionSize,
    IN WVL_E_DISK_MEDIA_TYPE MediaType,
    OUT PDEVICE_OBJECT * Pdo
  ) {
    /* Floppy, hard disk, optical disc specifics. */
    static const DEVICE_TYPE disk_types[WvlDiskMediaTypes] = {
        FILE_DEVICE_DISK,
        FILE_DEVICE_DISK,
        FILE_DEVICE_CD_ROM
      };
    static const UINT32 characteristics[WvlDiskMediaTypes] = {
        FILE_REMOVABLE_MEDIA | FILE_FLOPPY_DISKETTE,
        0,
        FILE_REMOVABLE_MEDIA | FILE_READ_ONLY_DEVICE
      };
    NTSTATUS status;
    PDEVICE_OBJECT pdo;

    /* Validate parameters. */
    status = STATUS_INVALID_PARAMETER;
    if (!DriverObj) {
        DBG("No driver passed!\n");
        goto err_driver_obj;
      }
    if (ExtensionSize < sizeof (WV_S_DEV_EXT)) {
        DBG("Extension size too small!\n");
        goto err_ext_size;
      }
    if (MediaType >= WvlDiskMediaTypes) {
        DBG("Unknown media type!\n");
        goto err_media_type;
      }
    if (!Pdo) {
        DBG("No PDO to fill!\n");
        goto err_pdo_fill;
      }

    /* Create the PDO. */
    status = IoCreateDevice(
        DriverObj,
        ExtensionSize,
        NULL,
        disk_types[MediaType],
        FILE_AUTOGENERATED_DEVICE_NAME |
          FILE_DEVICE_SECURE_OPEN |
          characteristics[MediaType],
        FALSE,
        &pdo
      );
    if (!NT_SUCCESS(status)) {
        DBG("Could not create disk PDO.\n");
        goto err_pdo;
      }

    *Pdo = pdo;
    return STATUS_SUCCESS;

    IoDeleteDevice(pdo);
    err_pdo:

    err_pdo_fill:

    err_media_type:

    err_ext_size:

    err_driver_obj:

    return status;
  }

/*
 * WVL_S_DISK_FAT_EXTRA and WVL_S_DISK_FAT_SUPER taken from
 * syslinux/memdisk/setup.c by H. Peter Anvin.  Licensed under the terms
 * of the GNU General Public License version 2 or later.
 */
#ifdef _MSC_VER
#  pragma pack(1)
#endif
struct WVL_DISK_FAT_EXTRA_ {
    UCHAR bs_drvnum;
    UCHAR bs_resv1;
    UCHAR bs_bootsig;
    UINT32 bs_volid;
    char bs_vollab[11];
    char bs_filsystype[8];
  } __attribute__((packed));
typedef struct WVL_DISK_FAT_EXTRA_
  WVL_S_DISK_FAT_EXTRA_, * WVL_SP_DISK_FAT_EXTRA_;

struct WVL_DISK_FAT_SUPER_ {
    UCHAR bs_jmpboot[3];
    char bs_oemname[8];
    UINT16 bpb_bytspersec;
    UCHAR bpb_secperclus;
    UINT16 bpb_rsvdseccnt;
    UCHAR bpb_numfats;
    UINT16 bpb_rootentcnt;
    UINT16 bpb_totsec16;
    UCHAR bpb_media;
    UINT16 bpb_fatsz16;
    UINT16 bpb_secpertrk;
    UINT16 bpb_numheads;
    UINT32 bpb_hiddsec;
    UINT32 bpb_totsec32;
    union {
        struct { WVL_S_DISK_FAT_EXTRA_ extra; } fat16;
        struct {
            UINT32 bpb_fatsz32;
            UINT16 bpb_extflags;
            UINT16 bpb_fsver;
            UINT32 bpb_rootclus;
            UINT16 bpb_fsinfo;
            UINT16 bpb_bkbootsec;
            char bpb_reserved[12];
            /* Clever, eh?  Same fields, different offset... */
            WVL_S_DISK_FAT_EXTRA_ extra;
          } fat32 __attribute__((packed));
      } x;
  } __attribute__((__packed__));
typedef struct WVL_DISK_FAT_SUPER_
  WVL_S_DISK_FAT_SUPER_, * WVL_SP_DISK_FAT_SUPER_;
#ifdef _MSC_VER
#  pragma pack()
#endif

/**
 * Attempt to guess a disk's geometry.
 *
 * @v BootSect          The MBR or VBR with possible geometry clues.
 * @v Disk              The disk to set the geometry for.
 */
WVL_M_LIB VOID WvlDiskGuessGeometry(
    IN WVL_AP_DISK_BOOT_SECT BootSect,
    IN OUT WV_SP_DISK_T Disk
  ) {
    UINT16 heads = 0, sects_per_track = 0, cylinders;
    WVL_SP_DISK_MBR as_mbr;

    if ((BootSect == NULL) || (Disk == NULL))
      return;

    /*
     * FAT superblock geometry checking taken from syslinux/memdisk/setup.c by
     * H. Peter Anvin.  Licensed under the terms of the GNU General Public
     * License version 2 or later.
     */
    {
        /*
         * Look for a FAT superblock and if we find something that looks
         * enough like one, use geometry from that.  This takes care of
         * megafloppy images and unpartitioned hard disks. 
         */
        WVL_SP_DISK_FAT_EXTRA_ extra = NULL;
        WVL_SP_DISK_FAT_SUPER_ fs = (WVL_SP_DISK_FAT_SUPER_) BootSect;
  
        if (
            (fs->bpb_media == 0xf0 || fs->bpb_media >= 0xf8) &&
            (fs->bs_jmpboot[0] == 0xe9 || fs->bs_jmpboot[0] == 0xeb) &&
            fs->bpb_bytspersec == 512 &&
            fs->bpb_numheads >= 1 &&
            fs->bpb_numheads <= 256 &&
            fs->bpb_secpertrk >= 1 &&
            fs->bpb_secpertrk <= 63
          ) {
            extra = fs->bpb_fatsz16 ? &fs->x.fat16.extra : &fs->x.fat32.extra;
            if (!(
                extra->bs_bootsig == 0x29 &&
                extra->bs_filsystype[0] == 'F' &&
                extra->bs_filsystype[1] == 'A' &&
                extra->bs_filsystype[2] == 'T'
              ))
              extra = NULL;
          }
        if (extra) {
            heads = fs->bpb_numheads;
            sects_per_track = fs->bpb_secpertrk;
          }
      } /* sub-scope */
    /*
     * If we couldn't parse a FAT superblock, try checking MBR params.
     * Logic derived from syslinux/memdisk/setup.c by H. Peter Anvin.
     */
    as_mbr = (WVL_SP_DISK_MBR) BootSect;
    if (
        (heads == 0 ) &&
        (sects_per_track == 0) &&
        (as_mbr->mbr_sig == 0xAA55)
      ) {
        int i;

        for (i = 0; i < 4; i++) {
            if (
                !(as_mbr->partition[i].status & 0x7f) &&
                as_mbr->partition[i].type
              ) {
                UCHAR h, s;

                h = chs_head(as_mbr->partition[i].chs_start) + 1;
                s = chs_sector(as_mbr->partition[i].chs_start);

                if (heads < h)
                  heads = h;
                if (sects_per_track < s)
                  sects_per_track = s;

                h = chs_head(as_mbr->partition[i].chs_end) + 1;
                s = chs_sector(as_mbr->partition[i].chs_end);

                if (heads < h)
                  heads = h;
                if (sects_per_track < s)
                  sects_per_track = s;
              } /* if */
          } /* for */
      } /* if */
    /* If we were unable to guess, use some hopeful defaults. */
    if (!heads)
      heads = 255;
    if (!sects_per_track)
      sects_per_track = 63;
    /* Set params that are not already filled. */
    if (!Disk->Heads)
      Disk->Heads = heads;
    if (!Disk->Sectors)
      Disk->Sectors = sects_per_track;
    if (!Disk->Cylinders)
      Disk->Cylinders = Disk->LBADiskSize / (heads * sects_per_track);
  }

/**
 * Initialize disk defaults.
 *
 * @v Disk              The disk to initialize.
 */
WVL_M_LIB VOID STDCALL WvlDiskInit(IN OUT WV_SP_DISK_T Disk) {
    RtlZeroMemory(Disk, sizeof *Disk);
    return;
  }

/* See WVL_F_DISK_IO in the header for details. */
WVL_M_LIB NTSTATUS STDCALL WvlDiskIo(
    IN WV_SP_DISK_T Disk,
    IN WVL_E_DISK_IO_MODE Mode,
    IN LONGLONG StartSector,
    IN UINT32 SectorCount,
    IN PUCHAR Buffer,
    IN PIRP Irp
  ) {
    if (Disk->disk_ops.Io) {
        return Disk->disk_ops.Io(
            Disk,
            Mode,
            StartSector,
            SectorCount,
            Buffer,
            Irp
          );
      }
    return WvlIrpComplete(Irp, 0, STATUS_DRIVER_INTERNAL_ERROR);
  }

/* See WVL_F_DISK_MAX_XFER_LEN in the header for details. */
WVL_M_LIB UINT32 WvlDiskMaxXferLen(IN WV_SP_DISK_T Disk) {
    /* Use the disk operation, if there is one. */
    if (Disk->disk_ops.MaxXferLen)
      return Disk->disk_ops.MaxXferLen(Disk);
    return 1024 * 1024;
  }
