/*
 *	include/linux/zy_feature_bits.h - Zyxel Feature Bits related definitions.
 *	Copyright (C) 2017 Zyxel Communications Corp.
 */

#ifndef _LINUX_ZY_FEATURE_BITS_H
#define _LINUX_ZY_FEATURE_BITS_H

/*!
 *  ZYFEATUREBITS_XXX :
 *  Zyxel Feature Bits offset are defined in linux header
 *  /linux-brcm963xx_xxx/linux-3.4.11/include/linux/zy_feature_bits.h,
 *  which is used by /bcmdrivers/opensource/include/bcm963xx/board.h
 *  and
 *  /libzyutil-1.0/zyutil.h
 */

#define ZYFEATUREBITS_MASK_ADSLTYPE     0x0000003F
#define ZYFEATUREBITS_MASK_VDSLPROFFILE 0x00000FC0
#define ZYFEATUREBITS_MASK_WIFI         0x000FF000
#define ZYFEATUREBITS_MASK_VOIP         0x00300000
#define ZYFEATUREBITS_MASK_RESERVED     0xFFC00000
#define ZYFEATUREBITS_ADSL_BASEBIT      0x1
#define ZYFEATUREBITS_ADSL_ANNEXA       (ZYFEATUREBITS_ADSL_BASEBIT << 0)
#define ZYFEATUREBITS_ADSL_ANNEXB       (ZYFEATUREBITS_ADSL_BASEBIT << 1)
#define ZYFEATUREBITS_ADSL_ANNEXC       (ZYFEATUREBITS_ADSL_BASEBIT << 2)
#define ZYFEATUREBITS_VDSL_17A          (0x40 << 0)
#define ZYFEATUREBITS_VDSL_35B          (0x40 << 1)
#define ZYFEATUREBITS_VDSL_GFAST        (0x40 << 2)
#define ZYFEATUREBITS_WIFI_24G          (0x1000 << 0)
#define ZYFEATUREBITS_WIFI_24G_FEM      (0x1000 << 2)
#define ZYFEATUREBITS_WIFI_24G_EXT_ANT  (0x1000 << 4)
#define ZYFEATUREBITS_WIFI_5G           (0x1000 << 1)
#define ZYFEATUREBITS_WIFI_5G_FEM       (0x1000 << 3)
#define ZYFEATUREBITS_WIFI_5G_EXT_ANT   (0x1000 << 5)
#define ZYFEATUREBITS_VOIP              (0x100000 << 0)
/*!
 *  MRDFEATUREBITS_XXX :
 *  these offset need to sync with /build_dir/host/brcm-imageutil-xxx/board.h
 */
#define MRDFEATUREBITS_BYTE_VOIP            8
#define MRDFEATUREBITS_BYTE_WIFI            9
#define MRDFEATUREBITS_BYTE_VDSLPROFILE     10
#define MRDFEATUREBITS_BYTE_ADSLTYPE        11
#define MRDFEATUREBITS_ADSL_ANNEXA          (0x1 << 0)
#define MRDFEATUREBITS_ADSL_ANNEXB          (0x1 << 1)
#define MRDFEATUREBITS_ADSL_ANNEXC          (0x1 << 2)
#define MRDFEATUREBITS_VDSL_17A             (0x1 << 0)
#define MRDFEATUREBITS_VDSL_35B             (0x1 << 1)
#define MRDFEATUREBITS_VDSL_GFAST           (0x1 << 2)
#define MRDFEATUREBITS_VDSL_RESERVERD_1     (0x1 << 3)
#define MRDFEATUREBITS_VDSL_RESERVERD_2     (0x1 << 4)
#define MRDFEATUREBITS_VDSL_RESERVERD_LAST  (0x1 << 5)
#define MRDFEATUREBITS_WIFI_24G             (0x1 << 0)
#define MRDFEATUREBITS_WIFI_24G_FEM         (0x1 << 2)
#define MRDFEATUREBITS_WIFI_24G_EXT_ANT     (0x1 << 4)
#define MRDFEATUREBITS_WIFI_5G              (0x1 << 1)
#define MRDFEATUREBITS_WIFI_5G_FEM          (0x1 << 3)
#define MRDFEATUREBITS_WIFI_5G_EXT_ANT      (0x1 << 5)
#define MRDFEATUREBITS_WIFI_RESERVERD_1     (0x1 << 6)
#define MRDFEATUREBITS_WIFI_RESERVERD_LAST  (0x1 << 7)
#define MRDFEATUREBITS_VOIP                 (0x1 << 0)
#define MRDFEATUREBITS_VOIP_RESERVERD_LAST  (0x1 << 1)

#endif	/* _LINUX_ZY_FEATURE_BITS_H */
