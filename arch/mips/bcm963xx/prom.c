#if defined(CONFIG_BCM_KF_MIPS_BCM963XX) && defined(CONFIG_MIPS_BCM963XX)
/*
* <:copyright-BRCM:2004:DUAL/GPL:standard
* 
*    Copyright (c) 2004 Broadcom 
*    All Rights Reserved
* 
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, version 2, as published by
* the Free Software Foundation (the "GPL").
* 
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* 
* 
* A copy of the GPL is available at http://www.broadcom.com/licenses/GPLv2.php, or by
* writing to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
* Boston, MA 02111-1307, USA.
* 
* :>
 
*/
/*
 * prom.c: PROM library initialization code.
 *
 */
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/bootmem.h>
#include <linux/blkdev.h>
#include <asm/addrspace.h>
#include <asm/bootinfo.h>
#include <asm/cpu.h>
#include <asm/time.h>

#include <bcm_map_part.h>
#include <bcm_cpu.h>
#include <board.h>
#include <boardparms.h>
#if defined(CONFIG_BCM96848)
#include <bcm_otp.h>
#endif

extern int  do_syslog(int, char *, int);

unsigned char g_blparms_buf[1024];

#if defined(CONFIG_BCM_KF_DSP) && defined(CONFIG_BCM_BCMDSP_MODULE)
unsigned int main_tp_num;
#endif

static void __init create_cmdline(char *cmdline);
UINT32 __init calculateCpuSpeed(void);
void __init retrieve_boot_loader_parameters(void);

#if defined (CONFIG_BCM96328)
const uint32 cpu_speed_table[0x20] = {
    320, 320, 320, 320, 320, 320, 320, 320, 320, 320, 320, 320, 320, 320, 320, 320,
    0, 320, 160, 200, 160, 200, 400, 320, 320, 160, 384, 320, 192, 320, 320, 320
};
#endif

#if defined (CONFIG_BCM96362)
const uint32 cpu_speed_table[0x20] = {
    320, 320, 320, 240, 160, 400, 440, 384, 320, 320, 320, 240, 160, 320, 400, 320,
    320, 320, 320, 240, 160, 200, 400, 384, 320, 320, 320, 240, 160, 200, 400, 400
};
#endif

#if defined (CONFIG_BCM963268)
const uint32 cpu_speed_table[0x20] = {
    0, 0, 400, 320, 0, 0, 0, 0, 0, 0, 333, 400, 0, 0, 320, 400,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
#endif

#if defined (CONFIG_BCM96318)
const uint32 cpu_speed_table[0x04] = {
    166, 400, 250, 333
};
#endif

#if defined (CONFIG_BCM960333)
const uint32 cpu_speed_table[0x04] = {
    200, 400, 333, 0
};
#endif






#if defined (CONFIG_BCM96838)
const uint32 cpu_speed_table[0x3] = {
    600, 400, 240
};
#endif

#if defined (CONFIG_BCM963381)
const uint32 cpu_speed_table[0x04] = {
    300, 800, 480, 600
};
#endif

#if defined (CONFIG_BCM96848)
const uint32 cpu_speed_table[8] = {
    250, 250, 400, 400, 250, 250, 428, 600 
};
#endif

static char promBoardIdStr[NVRAM_BOARD_ID_STRING_LEN];
const char *get_system_type(void)
{
    kerSysNvRamGetBoardId(promBoardIdStr);
    return(promBoardIdStr);
}


/* --------------------------------------------------------------------------
    Name: prom_init
 -------------------------------------------------------------------------- */

extern struct plat_smp_ops brcm_smp_ops;

void __init prom_init(void)
{
    int argc = fw_arg0;
    u32 *argv = (u32 *)CKSEG0ADDR(fw_arg1);
    int i;

    retrieve_boot_loader_parameters();
    kerSysEarlyFlashInit();

    // too early in bootup sequence to acquire spinlock, not needed anyways
    // only the kernel is running at this point
    kerSysNvRamGetBoardIdLocked(promBoardIdStr);
    printk( "%s prom init\n", promBoardIdStr );

#if defined(CONFIG_BCM_KF_DSP) && defined(CONFIG_BCM_BCMDSP_MODULE)
    main_tp_num = ((read_c0_diag3() & CP0_CMT_TPID) == CP0_CMT_TPID) ? 1 : 0;
    printk("Linux TP ID = %u \n", (unsigned int)main_tp_num);
#endif

    PERF->IrqControl[0].IrqMask=0;

    arcs_cmdline[0] = '\0';

    create_cmdline(arcs_cmdline);

    strcat(arcs_cmdline, " ");

    for (i = 1; i < argc; i++) {
        strcat(arcs_cmdline, (char *)CKSEG0ADDR(argv[i]));
        if (i < (argc - 1))
            strcat(arcs_cmdline, " ");
    }


    /* Count register increments every other clock */
    mips_hpt_frequency = calculateCpuSpeed() / 2;

#if defined (CONFIG_SMP)
    register_smp_ops(&brcm_smp_ops);
#endif
}


/* --------------------------------------------------------------------------
    Name: prom_free_prom_memory
Abstract: 
 -------------------------------------------------------------------------- */
void __init prom_free_prom_memory(void)
{

}

#if defined(CONFIG_ROOT_NFS) && defined(SUPPORT_SWMDK)
  /* We can't use gendefconfig to automatically fix this, so instead we will
     raise an error here */
  #error "Kernel cannot be configured for both SWITCHMDK and NFS."
#endif

#define HEXDIGIT(d) ((d >= '0' && d <= '9') ? (d - '0') : ((d | 0x20) - 'W'))
#define HEXBYTE(b)  (HEXDIGIT((b)[0]) << 4) + HEXDIGIT((b)[1])

#ifndef CONFIG_ROOT_NFS_DIR
#define CONFIG_ROOT_NFS_DIR	"h:/"
#endif

#ifdef CONFIG_BLK_DEV_RAM_SIZE
#define RAMDISK_SIZE		CONFIG_BLK_DEV_RAM_SIZE
#else
#define RAMDISK_SIZE		0x800000
#endif

/*
 * This function reads in a line that looks something like this from NvRam:
 *
 * CFE bootline=bcmEnet(0,0)host:vmlinux e=192.169.0.100:ffffff00 h=192.169.0.1
 *
 * and retuns in the cmdline parameter based on the boot_type that CFE sets up.
 *
 * for boot from flash, it will use the definition in CONFIG_ROOT_FLASHFS
 *
 * for boot from NFS, it will look like below:
 * CONFIG_CMDLINE="root=/dev/nfs nfsroot=192.168.0.1:/opt/targets/96345R/fs
 * ip=192.168.0.100:192.168.0.1::255.255.255.0::eth0:off rw"
 *
 * for boot from tftp, it will look like below:
 * CONFIG_CMDLINE="root=/dev/ram rw rd_start=0x81000000 rd_size=0x1800000"
 */
static void __init create_cmdline(char *cmdline)
{
	char boot_type = '\0', mask[16] = "";
	char bootline[NVRAM_BOOTLINE_LEN] = "";
	char *localip = NULL, *hostip = NULL, *p = bootline, *rdaddr = NULL;

	/*
	 * too early in bootup sequence to acquire spinlock, not needed anyways
	 * only the kernel is running at this point
	 */
	kerSysNvRamGetBootlineLocked(bootline);

	while (*p) {
		if (p[0] == 'e' && p[1] == '=') {
			/* Found local ip address */
			p += 2;
			localip = p;
			while (*p && *p != ' ' && *p != ':')
				p++;
			if (*p == ':') {
				/* Found network mask (eg FFFFFF00 */
				*p++ = '\0';
				sprintf(mask, "%u.%u.%u.%u", HEXBYTE(p),
					HEXBYTE(p + 2),
				HEXBYTE(p + 4), HEXBYTE(p + 6));
				p += 4;
			} else if (*p == ' ')
				*p++ = '\0';
		} else if (p[0] == 'h' && p[1] == '=') {
			/* Found host ip address */
			p += 2;
			hostip = p;
			while (*p && *p != ' ')
				p++;
			if (*p == ' ')
				*p++ = '\0';
		} else if (p[0] == 'r' && p[1] == '=') {
			/* Found boot type */
			p += 2;
			boot_type = *p;
			while (*p && *p != ' ')
				p++;
			if (*p == ' ')
				*p++ = '\0';
		} else if (p[0] == 'a' && p[1] == '=') {
			p += 2;
			rdaddr = p;
			while (*p && *p != ' ')
				p++;
			if (*p == ' ')
				*p++ = '\0';
		} else 
			p++;
	}

	if (boot_type == 'h' && localip && hostip) {
		/* Boot from NFS with proper IP addresses */
		sprintf(cmdline, "root=/dev/nfs nfsroot=%s:" CONFIG_ROOT_NFS_DIR
				" ip=%s:%s::%s::eth0:off rw",
				hostip, localip, hostip, mask);
	} else if (boot_type == 'c') {
		/* boot from tftp */
		sprintf(cmdline, "root=/dev/ram0 ro rd_start=%s rd_size=0x%x",
				rdaddr, RAMDISK_SIZE << 10);
	} else {
		/* go with the default, boot from flash */
#ifdef CONFIG_ROOT_FLASHFS
		strcpy(cmdline, CONFIG_ROOT_FLASHFS);
#endif
	}
}

/*  *********************************************************************
    *  calculateCpuSpeed()
    *      Calculate the BCM63xx CPU speed by reading the PLL Config register
    *      and applying the following formula:
    *      Fcpu_clk = (25 * MIPSDDR_NDIV) / MIPS_MDIV
    *  Input parameters:
    *      none
    *  Return value:
    *      none
    ********************************************************************* */

#if defined(CONFIG_BCM96328) || defined(CONFIG_BCM96362) ||         defined(CONFIG_BCM963268) ||                                         defined(CONFIG_BCM963381)
UINT32 __init calculateCpuSpeed(void)
{
    UINT32 mips_pll_fvco;

    mips_pll_fvco = MISC->miscStrapBus & MISC_STRAP_BUS_MIPS_PLL_FVCO_MASK;
    mips_pll_fvco >>= MISC_STRAP_BUS_MIPS_PLL_FVCO_SHIFT;

    return cpu_speed_table[mips_pll_fvco] * 1000000;
}
#endif

#if defined(CONFIG_BCM96318) || defined(CONFIG_BCM960333)
UINT32 __init calculateCpuSpeed(void)
{
	UINT32 uiCpuSpeedTableIdx;				// Index into the CPU speed table (0 to 3)
	
	// Get the strapOverrideBus bits to index into teh CPU speed table	
	uiCpuSpeedTableIdx = STRAP->strapOverrideBus & STRAP_BUS_MIPS_FREQ_MASK;
	uiCpuSpeedTableIdx >>= STRAP_BUS_MIPS_FREQ_SHIFT;
    
    return cpu_speed_table[uiCpuSpeedTableIdx] * 1000000;
}
#endif

#if defined(CONFIG_BCM96838)
UINT32 __init calculateCpuSpeed(void)
{ 
#define OTP_BASE		   0xb4e00400
#define OTP_SHADOW_BRCM_BITS_0_31               0x40
#define OTP_BRCM_VIPER_FREQ_SHIFT               18
#define OTP_BRCM_VIPER_FREQ_MASK                (0x7 << OTP_BRCM_VIPER_FREQ_SHIFT)

    UINT32 otp_shadow_reg = *((volatile UINT32*)(OTP_BASE+OTP_SHADOW_BRCM_BITS_0_31));
	UINT32 uiCpuSpeedTableIdx = (otp_shadow_reg & OTP_BRCM_VIPER_FREQ_MASK) >> OTP_BRCM_VIPER_FREQ_SHIFT;
	
	return cpu_speed_table[uiCpuSpeedTableIdx] * 1000000;
}
#endif

#if defined(CONFIG_BCM96848)
UINT32 __init calculateCpuSpeed(void)
{
    UINT32 clock_sel_strap = (MISC->miscStrapBus & MISC_STRAP_CLOCK_SEL_MASK) >> MISC_STRAP_CLOCK_SEL_SHIFT;
    UINT32 clock_sel_otp = bcm_otp_get_max_clksel();
 
    if (cpu_speed_table[clock_sel_strap] <= cpu_speed_table[clock_sel_otp])
        return cpu_speed_table[clock_sel_strap] * 1000000;
    else
        return cpu_speed_table[clock_sel_otp] * 1000000;
}
#endif


/* Retrieve a buffer of paramters passed by the boot loader.  Functions in
 * board.c can return requested parameter values to a calling Linux function.
 */
void __init retrieve_boot_loader_parameters(void)
{
    extern unsigned char _text;
    unsigned long blparms_magic = *(unsigned long *) (&_text - 8);
    unsigned long blparms_buf = *(unsigned long *) (&_text - 4);
    unsigned char *src = (unsigned char *) blparms_buf;
    unsigned char *dst = g_blparms_buf;

    if( blparms_magic != BLPARMS_MAGIC )
    {
        /* Subtract four more bytes for NAND flash images. */
        blparms_magic = *(unsigned long *) (&_text - 12);
        blparms_buf = *(unsigned long *) (&_text - 8);
        src = (unsigned char *) blparms_buf;
    }

    if( blparms_magic == BLPARMS_MAGIC )
    {
        do
        {
            *dst++ = *src++;
        } while( (src[0] != '\0' || src[1] != '\0') &&
          (unsigned long) (dst - g_blparms_buf) < sizeof(g_blparms_buf) - 2);
    }

    dst[0] = dst[1] = '\0';
}

#endif // defined(CONFIG_BCM_KF_MIPS_BCM963XX)

