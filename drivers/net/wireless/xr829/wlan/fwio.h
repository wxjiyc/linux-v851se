/*
 * Firmware APIs for XRadio drivers
 *
 * Copyright (c) 2013
 * Xradio Technology Co., Ltd. <www.xradiotech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef FWIO_H_INCLUDED
#define FWIO_H_INCLUDED

#define XR829_HW_REV0       (8290)
#ifdef USE_VFS_FIRMWARE
#define XR829_BOOTLOADER    ("/lib/firmware/boot_xr829.bin")
#define XR829_FIRMWARE      ("/lib/firmware/fw_xr829.bin")
#define XR829_SDD_FILE      ("/lib/firmware/sdd_xr829.bin")
#define XR829_ETF_FIRMWARE   ("/lib/firmware/etf_xr829.bin")
#else
#define XR829_BOOTLOADER    ("boot_xr829.bin")
#define XR829_FIRMWARE      ("fw_xr829.bin")
#define XR829_SDD_FILE      ("sdd_xr829.bin")
#define XR829_ETF_FIRMWARE  ("etf_xr829.bin")
#endif

#define SDD_PTA_CFG_ELT_ID              0xEB
#define SDD_REFERENCE_FREQUENCY_ELT_ID  0xC5
#define SDD_MAX_OUTPUT_POWER_2G4_ELT_ID 0xE3
#define SDD_MAX_OUTPUT_POWER_5G_ELT_ID  0xE4

#define FIELD_OFFSET(type, field) ((u8 *)&((type *)0)->field - (u8 *)0)
#define FIND_NEXT_ELT(e) (struct xradio_sdd *)((u8 *)&e->data + e->length)
struct xradio_sdd {
	u8 id;
	u8 length;
	u8 data[];
};

struct xradio_common;
int xradio_load_firmware(struct xradio_common *hw_priv);
int xradio_dev_deinit(struct xradio_common *hw_priv);
int xradio_update_dpllctrl(struct xradio_common *hw_priv, u32 dpll_update);

#endif
