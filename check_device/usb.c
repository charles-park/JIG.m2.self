//------------------------------------------------------------------------------
/**
 * @file usb.c
 * @author charles-park (charles.park@hardkernel.com)
 * @brief Device Test library for ODROID-JIG.
 * @version 0.2
 * @date 2023-10-12
 *
 * @package apt install iperf3, nmap, ethtool, usbutils, alsa-utils
 *
 * @copyright Copyright (c) 2022
 *
 */
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>

//------------------------------------------------------------------------------
#include "usb.h"

//------------------------------------------------------------------------------
#define STR_PATH_LENGTH 128

struct device_usb {
    // Control path
    char path[STR_PATH_LENGTH +1];
    // compare value (read min, write min : MB/s)
    int r_min, w_min;
    // Link speed
    int speed;

    // read value (Init)
    int value;
};

// default link speed, read/write (MB/s)
#define DEFAULT_USB30_L 5000
#define DEFAULT_USB30_R 100
#define DEFAULT_USB30_W 35

#define DEFAULT_USB20_L 480
#define DEFAULT_USB20_R 25
#define DEFAULT_USB20_W 20

//------------------------------------------------------------------------------
//
// Configuration
//
//------------------------------------------------------------------------------
/* define usb devices (USB3.0 eMMC reader with eMMC) */
//------------------------------------------------------------------------------
struct device_usb DeviceUSB [eUSB_END] = {
    // path, r_min(MB/s), w_min(MB/s), link, read
    // eUSB_30, USB 3.0
    { "/sys/bus/usb/devices/6-1", DEFAULT_USB30_R, DEFAULT_USB30_W, DEFAULT_USB30_L, 0 },
    // eUSB_20, USB 2.0
    { "/sys/bus/usb/devices/1-1", DEFAULT_USB20_R, DEFAULT_USB20_W, DEFAULT_USB20_L, 0 },
    // eUSB_C1, USB 3.0 up side
    { "/sys/bus/usb/devices/9-1", DEFAULT_USB30_R, DEFAULT_USB30_W, DEFAULT_USB30_L, 0 },
    // eUSB_C2, USB 3.0 dn side
    { "/sys/bus/usb/devices/9-1", DEFAULT_USB30_R, DEFAULT_USB30_W, DEFAULT_USB30_L, 0 },
};

// USB Read / Write (16 Mbytes, 1 block count)
#define USB_R_CHECK "dd of=/dev/null bs=16M count=1 iflag=nocache,dsync oflag=nocache,dsync if=/dev/"
#define USB_W_CHECK "dd if=/dev/zero bs=16M count=1 iflag=nocache,dsync oflag=nocache,dsync of=/dev/"

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
static int usb_speed (const char *path)
{
    FILE *fp;
    char cmd[STR_PATH_LENGTH], rdata[STR_PATH_LENGTH];

    if (access (path, R_OK) == 0) {
        memset  (cmd, 0x00, sizeof(cmd));
        sprintf (cmd, "%s/speed", path);
        if ((fp = fopen (cmd, "r")) != NULL) {
            memset (rdata, 0, sizeof(rdata));
            fgets  (rdata, sizeof(rdata), fp);
            fclose (fp);
        }
        return atoi (rdata);
    }
    return 0;
}

//------------------------------------------------------------------------------
static int usb_rw (const char *path, const char *check_cmd)
{
    FILE *fp;
    char cmd[STR_PATH_LENGTH], rdata[STR_PATH_LENGTH], *ptr;

    memset  (cmd, 0x00, sizeof(cmd));
    sprintf (cmd, "find %s/ -name sd* 2>&1", path);

    if ((fp = popen (cmd, "r")) != NULL) {
        memset (rdata, 0x00, sizeof(cmd));
        // 1 line read
        fgets (rdata, sizeof(rdata), fp);
        pclose (fp);
        // find string "sd"
        if ((ptr = strstr (rdata, "sd")) != NULL) {
            memset  (cmd, 0, sizeof (cmd));
            // 0x00 -> 0x20 (space)
            ptr[strlen(ptr)-1] = ' ';
            sprintf (cmd, "%s%s 2>&1", check_cmd, ptr);

            if ((fp = popen(cmd, "r")) != NULL) {
                while (1) {
                    memset (rdata, 0, sizeof (rdata));
                    if ((fgets (rdata, sizeof (rdata), fp)) == NULL)
                        break;
                    if ((ptr = strstr (rdata, " MB/s")) != NULL) {
                        while (*ptr != ',') ptr--;
                        return atoi (ptr+1);
                    }
                }
                pclose (fp);
            }
        }
    }
    return 0;
}

//------------------------------------------------------------------------------
static int usb_c_side (const char *path, int id)
{
    FILE *fp;
    char cmd[STR_PATH_LENGTH], rdata[STR_PATH_LENGTH], *ptr;
    unsigned long size = 0;

    memset  (cmd, 0x00, sizeof(cmd));
    sprintf (cmd, "find %s/ -name size* 2>&1", path);

    printf ("%s(%d)\n", __func__, __LINE__);
    if ((fp = popen (cmd, "r")) != NULL) {
        memset (rdata, 0x00, sizeof(rdata));
        // 1 line read
        fgets (rdata, sizeof(rdata), fp);
        pclose (fp);

        printf ("%s(%d) : rdata = %s\n", __func__, __LINE__, rdata);
        if (!strlen(rdata))     return 0;

        memset (cmd, 0x00, sizeof(cmd));
        snprintf (cmd, sizeof(cmd), "cat %s", rdata);

        if ((fp = popen (cmd, "r")) != NULL) {
            memset (rdata, 0x00, sizeof(rdata));
            if ((fgets (rdata, sizeof(rdata), fp)) != NULL) {
                size = atol(rdata);
                printf ("%s : id = %d, size = %ld\n", __func__, id, size);
            }
            pclose (fp);
        }
    }

#define USB_C1_SIZE_H   40000000
#define USB_C1_SIZE_L   30000000
#define USB_C2_SIZE_H   16000000
#define USB_C2_SIZE_L   15000000

    switch (id) {
        case eUSB_C1:   // 16GB eMMC Reader
            if ((size > USB_C1_SIZE_L) && (size < USB_C1_SIZE_H))   return 1;
            break;
        case eUSB_C2:   // 8GB eMMC Reader
            if ((size > USB_C2_SIZE_L) && (size < USB_C2_SIZE_H))   return 1;
            break;
        default:
            return 1;
    }
    return 0;
}

//------------------------------------------------------------------------------
int usb_check (int id)
{
    int value = 0;

    if ((id >= eUSB_END) || ((access (DeviceUSB[id].path, R_OK)) != 0)) {
        return 0;
    }

    if (usb_speed (DeviceUSB[id].path) != DeviceUSB[id].speed)
        return 0;

    switch (id) {
        case eUSB_C1_W: case eUSB_C2_W:
        case eUSB_30_W: case eUSB_20_W:
            value = usb_rw (DeviceUSB[id].path, USB_W_CHECK);
            return (value > DeviceUSB[id].w_min) ? value : 0;
        default :
            if (usb_c_side (DeviceUSB[id].path, id))
                value = usb_rw (DeviceUSB[id].path, USB_R_CHECK);

            return (value > DeviceUSB[id].r_min) ? value : 1;
    }
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
