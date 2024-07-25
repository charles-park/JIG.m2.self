//------------------------------------------------------------------------------
/**
 * @file client.c
 * @author charles-park (charles.park@hardkernel.com)
 * @brief ODROID-M1S JIG Client App.
 * @version 0.2
 * @date 2023-10-23
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
#include <time.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <linux/fb.h>
#include <getopt.h>

//------------------------------------------------------------------------------
#include "lib_fbui/lib_fb.h"
#include "lib_fbui/lib_ui.h"
#include "nlp_server_ctrl/nlp_server_ctrl.h"
#include "lib_mac/lib_mac.h"
#include "lib_efuse/lib_efuse.h"
#include "lib_gpio/lib_gpio.h"

#include "check_device/adc.h"
#include "check_device/hdmi.h"
#include "check_device/system.h"
#include "check_device/storage.h"
#include "check_device/usb.h"
#include "check_device/ethernet.h"

//------------------------------------------------------------------------------
//
// JIG Protocol(V2.0)
// https://docs.google.com/spreadsheets/d/1Of7im-2I5m_M-YKswsubrzQAXEGy-japYeH8h_754WA/edit#gid=0
//
//------------------------------------------------------------------------------
#define DEVICE_FB   "/dev/fb0"
#define CONFIG_UI   "m2.cfg"

#define ALIVE_DISPLAY_UI_ID     0
#define ALIVE_DISPLAY_INTERVAL  1000

#define APP_LOOP_DELAY  500

#define TIMEOUT_SEC     60

//------------------------------------------------------------------------------
#define IP_ADDR_SIZE    20

typedef struct client__t {
    // HDMI UI
    fb_info_t   *pfb;
    ui_grp_t    *pui;

    // nlp for mac
//    struct nlp_info nlp;
    char board_ip [IP_ADDR_SIZE];
    char nlp_ip   [IP_ADDR_SIZE];
    char efuse_data [EFUSE_UUID_SIZE +1];
    char mac [MAC_STR_SIZE +1];
}   client_t;

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// 문자열 변경 함수. 입력 포인터는 반드시 메모리가 할당되어진 변수여야 함.
//------------------------------------------------------------------------------
static void tolowerstr (char *p)
{
    int i, c = strlen(p);

    for (i = 0; i < c; i++, p++)
        *p = tolower(*p);
}

//------------------------------------------------------------------------------
static void toupperstr (char *p)
{
    int i, c = strlen(p);

    for (i = 0; i < c; i++, p++)
        *p = toupper(*p);
}

//------------------------------------------------------------------------------
static int run_interval_check (struct timeval *t, double interval_ms)
{
    struct timeval base_time;
    double difftime;

    gettimeofday(&base_time, NULL);

    if (interval_ms) {
        /* 현재 시간이 interval시간보다 크면 양수가 나옴 */
        difftime = (base_time.tv_sec - t->tv_sec) +
                    ((base_time.tv_usec - (t->tv_usec + interval_ms * 1000)) / 1000000);

        if (difftime > 0) {
            t->tv_sec  = base_time.tv_sec;
            t->tv_usec = base_time.tv_usec;
            return 1;
        }
        return 0;
    }
    /* 현재 시간 저장 */
    t->tv_sec  = base_time.tv_sec;
    t->tv_usec = base_time.tv_usec;
    return 1;
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
enum {
    eSTATUS_NONE = 0,
    eSTATUS_RUN,
    eSTATUS_FAIL,
    eSTATUS_PASS,
    eSTATUS_END
};

enum {
    eITEM_SERVER = 0,
    eITEM_IPERF,

    eITEM_MAC_ADDR,

    // hdmi
    eITEM_EDID,
    eITEM_HPD,
    // adc
    eITEM_ADC37,
    eITEM_ADC40,
    // system
    eITEM_FB,
    eITEM_MEM,
    // storage
    eITEM_eMMC,
    eITEM_uSD,
    eITEM_NVME,

    // usb
    eITEM_USB30,
    eITEM_USB20,
    eITEM_USB_C,

    // HP_DETECT
    eITEM_HP_DET,

    eITEM_SW_ADC,

    eITEM_ETHERNET_100M,
    eITEM_ETHERNET_1G,

    eITEM_END
};

char ItemStatus [eITEM_END] = {0,};

//------------------------------------------------------------------------------
#define UI_STATUS   47
#define	RUN_BOX_ON	RGB_TO_UINT(204, 204, 0)
#define	RUN_BOX_OFF	RGB_TO_UINT(153, 153, 0)

void *check_status (void *arg);
void *check_status (void *arg)
{
    static int onoff = 0;
    client_t *p = (client_t *)arg;
    static int TimeoutStop = TIMEOUT_SEC;

    while (TimeoutStop) {
        ui_set_ritem (p->pfb, p->pui, ALIVE_DISPLAY_UI_ID,
                    onoff ? COLOR_GREEN : p->pui->bc.uint, -1);
        onoff = !onoff;

        if ((ItemStatus [eITEM_SERVER] == eSTATUS_PASS) && TimeoutStop) {
            char str [16];

            memset (str, 0, sizeof(str));
            sprintf (str, "RUNNING %d", TimeoutStop);
            ui_set_sitem (p->pfb, p->pui, UI_STATUS, -1, -1, str);
            ui_set_ritem (p->pfb, p->pui, UI_STATUS, onoff ? RUN_BOX_ON : RUN_BOX_OFF, -1);
        }
        if (onoff)  {
            ui_update (p->pfb, p->pui, -1);
            if (TimeoutStop)    TimeoutStop--;
        }

        usleep (APP_LOOP_DELAY * 1000);
    }
    while (1)   sleep(1);
}

//------------------------------------------------------------------------------
#define HP_DET_GPIO 61
#define UI_DET_H    198
#define UI_DET_L    199

void *check_hp_detect (void *arg);
void *check_hp_detect (void *arg)
{
    int value = 0, new_value = 0, status = 0;

    client_t *p = (client_t *)arg;

    gpio_export    (HP_DET_GPIO);
    gpio_direction (HP_DET_GPIO, GPIO_DIR_IN);
    gpio_get_value (HP_DET_GPIO, &value);

    while (1) {
        if (gpio_get_value (HP_DET_GPIO, &new_value)) {
            if (value != new_value) {
                value = new_value;
                status |= value ? 0x02 : 0x01;

                if (value) {
                    ui_set_sitem (p->pfb, p->pui, UI_DET_H, -1, -1, "PASS");
                    ui_set_ritem (p->pfb, p->pui, UI_DET_H, COLOR_GREEN, -1);
                } else {
                    ui_set_sitem (p->pfb, p->pui, UI_DET_L, -1, -1, "PASS");
                    ui_set_ritem (p->pfb, p->pui, UI_DET_L, COLOR_GREEN, -1);
                }
            }
        }
        usleep (APP_LOOP_DELAY * 1000);
        if (status == 0x03)  ItemStatus[eITEM_HP_DET] = eSTATUS_PASS;
        if (ItemStatus [eITEM_HP_DET] == eSTATUS_PASS)  break;
    }
    gpio_unexport  (HP_DET_GPIO);
}

//------------------------------------------------------------------------------
#define SW_ADC_PATH "/sys/bus/iio/devices/iio:device0/in_voltage0_raw"
#define UI_SW_eMMC  178
#define UI_SW_uSD   179

// emmc : 1380 - 1390 - 1400
// sd : 680 - 690 - 700
int sw_adc_read (const char *path)
{
    char rdata[16];
    FILE *fp;

    memset (rdata, 0, sizeof (rdata));
    // adc raw value get
    if ((fp = fopen(path, "r")) != NULL) {
        fgets (rdata, sizeof(rdata), fp);
        fclose(fp);
    }
    return atoi(rdata);
}

#define SW_eMMC_MIN 1380
#define SW_eMMC_MAX 1400
#define SW_uSD_MIN  680
#define SW_uSD_MAX  700

void *check_sw_adc (void *arg);
void *check_sw_adc (void *arg)
{
    int value = 0, new_value = 0, status = 0, adc_value = 0;

    client_t *p = (client_t *)arg;

    adc_value = sw_adc_read (SW_ADC_PATH);

    if ((SW_eMMC_MIN < adc_value) && (SW_eMMC_MAX > adc_value ))
        value = 0;

    if ((SW_uSD_MIN < adc_value) && (SW_uSD_MAX > adc_value ))
        value = 1;

    while (1) {
        adc_value = sw_adc_read (SW_ADC_PATH);

        if ((SW_eMMC_MIN < adc_value) && (SW_eMMC_MAX > adc_value))
            new_value = 0;

        if ((SW_uSD_MIN < adc_value) && (SW_uSD_MAX > adc_value))
            new_value = 1;

        if (value != new_value) {
            value = new_value;
            status |= value ? 0x02 : 0x01;

            if (value) {
                ui_set_sitem (p->pfb, p->pui, UI_SW_uSD, -1, -1, "PASS");
                ui_set_ritem (p->pfb, p->pui, UI_SW_uSD, COLOR_GREEN, -1);
            } else {
                ui_set_sitem (p->pfb, p->pui, UI_SW_eMMC, -1, -1, "PASS");
                ui_set_ritem (p->pfb, p->pui, UI_SW_eMMC, COLOR_GREEN, -1);
            }
        }

        usleep (APP_LOOP_DELAY * 1000);
        if (status == 0x03)  ItemStatus[eITEM_SW_ADC] = eSTATUS_PASS;
        if (ItemStatus [eITEM_SW_ADC] == eSTATUS_PASS)  break;
    }
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
//#define LINK_SPEED_1G       1000
//#define LINK_SPEED_100M     100

#define UI_ETHERNET_100M    152
#define UI_ETHERNET_1G      153
#define UI_ETHERNET_SWITCH  154

void *check_device_ethernet (void *arg)
{
    int speed;
    client_t *p = (client_t *)arg;

    ItemStatus [eITEM_ETHERNET_100M] = eSTATUS_FAIL;
    ItemStatus [eITEM_ETHERNET_1G]   = eSTATUS_FAIL;

    while (1) {
        switch (speed) {
            case LINK_SPEED_1G:
                if (ethernet_link_setup (LINK_SPEED_100M)) {
                    ItemStatus [eITEM_ETHERNET_100M] = eSTATUS_PASS;
                    ui_set_sitem (p->pfb, p->pui, UI_ETHERNET_100M, -1, -1, "PASS");
                    ui_set_ritem (p->pfb, p->pui, UI_ETHERNET_100M, COLOR_GREEN, -1);
                }
                break;
            case LINK_SPEED_100M:
                if (ethernet_link_setup (LINK_SPEED_1G)) {
                    ItemStatus [eITEM_ETHERNET_1G] = eSTATUS_PASS;
                    ui_set_sitem (p->pfb, p->pui, UI_ETHERNET_1G, -1, -1, "PASS");
                    ui_set_ritem (p->pfb, p->pui, UI_ETHERNET_1G, COLOR_GREEN, -1);
                }
                break;
            default :
                break;
        }
        speed = ethernet_link_check ();
        if ((ItemStatus [eITEM_ETHERNET_1G]   == eSTATUS_PASS) &&
            (ItemStatus [eITEM_ETHERNET_100M] == eSTATUS_PASS)) {
            if (speed == LINK_SPEED_100M)
                ui_set_sitem (p->pfb, p->pui, UI_ETHERNET_SWITCH, -1, -1, "GREEN");
            else
                ui_set_sitem (p->pfb, p->pui, UI_ETHERNET_SWITCH, -1, -1, "ORANGE");

            ui_set_ritem (p->pfb, p->pui, UI_ETHERNET_SWITCH, RUN_BOX_ON, -1);
        }
        sleep (1);
    }
    return 0;
}

//------------------------------------------------------------------------------
#define UI_USB30    102
#define UI_USB20    107
#define UI_USB_C    122

static int check_device_usb (client_t *p)
{
    int value = 0;
    char str[10];

    // USB30
    ui_set_ritem (p->pfb, p->pui, UI_USB30, COLOR_YELLOW, -1);
    value = usb_check (eUSB_30);
    memset (str, 0, sizeof(str));   sprintf(str, "%d MB/s", value);

    ui_set_sitem (p->pfb, p->pui, UI_USB30, -1, -1, str);
    ui_set_ritem (p->pfb, p->pui, UI_USB30, value ? COLOR_GREEN : COLOR_RED, -1);
    ItemStatus[eITEM_USB30] = value ? eSTATUS_PASS : eSTATUS_FAIL;

    // USB20
    ui_set_ritem (p->pfb, p->pui, UI_USB20, COLOR_YELLOW, -1);
    value = usb_check (eUSB_20);
    memset (str, 0, sizeof(str));   sprintf(str, "%d MB/s", value);

    ui_set_sitem (p->pfb, p->pui, UI_USB20, -1, -1, str);
    ui_set_ritem (p->pfb, p->pui, UI_USB20, value ? COLOR_GREEN : COLOR_RED, -1);
    ItemStatus[eITEM_USB20] = value ? eSTATUS_PASS : eSTATUS_FAIL;

    // USB_C
    ui_set_ritem (p->pfb, p->pui, UI_USB_C, COLOR_YELLOW, -1);
    value = usb_check (eUSB_C);
    memset (str, 0, sizeof(str));   sprintf(str, "%d MB/s", value);

    ui_set_sitem (p->pfb, p->pui, UI_USB_C, -1, -1, str);
    ui_set_ritem (p->pfb, p->pui, UI_USB_C, value ? COLOR_GREEN : COLOR_RED, -1);
    ItemStatus[eITEM_USB_C] = value ? eSTATUS_PASS : eSTATUS_FAIL;

    return 1;
}

//------------------------------------------------------------------------------
#define UI_eMMC 62
#define UI_uSD  82
#define UI_NVME 87

static int check_device_storage (client_t *p)
{
    int value = 0;
    char str[10];

    // eMMC
    ui_set_ritem (p->pfb, p->pui, UI_eMMC, COLOR_YELLOW, -1);
    value = storage_check (eSTORAGE_eMMC);
    memset (str, 0, sizeof(str));   sprintf(str, "%d MB/s", value);

    ui_set_sitem (p->pfb, p->pui, UI_eMMC, -1, -1, str);
    ui_set_ritem (p->pfb, p->pui, UI_eMMC, value ? COLOR_GREEN : COLOR_RED, -1);
    ItemStatus[eITEM_eMMC] = value ? eSTATUS_PASS : eSTATUS_FAIL;

    // uSD
    ui_set_ritem (p->pfb, p->pui, UI_uSD, COLOR_YELLOW, -1);
    value = storage_check (eSTORAGE_uSD);
    memset (str, 0, sizeof(str));   sprintf(str, "%d MB/s", value);

    ui_set_sitem (p->pfb, p->pui, UI_uSD, -1, -1, str);
    ui_set_ritem (p->pfb, p->pui, UI_uSD, value ? COLOR_GREEN : COLOR_RED, -1);
    ItemStatus[eITEM_uSD] = value ? eSTATUS_PASS : eSTATUS_FAIL;

    // NVME
    ui_set_ritem (p->pfb, p->pui, UI_NVME, COLOR_YELLOW, -1);
    value = storage_check (eSTORAGE_NVME);
    memset (str, 0, sizeof(str));   sprintf(str, "%d MB/s", value);

    ui_set_sitem (p->pfb, p->pui, UI_NVME, -1, -1, str);
    ui_set_ritem (p->pfb, p->pui, UI_NVME, value ? COLOR_GREEN : COLOR_RED, -1);
    ItemStatus[eITEM_NVME] = value ? eSTATUS_PASS : eSTATUS_FAIL;

    return 1;
}

//------------------------------------------------------------------------------
#define UI_MEM      8
#define UI_FB       52

static int check_device_system (client_t *p)
{
    int value = 0;
    char str[10];

    // MEM
    ui_set_ritem (p->pfb, p->pui, UI_MEM, COLOR_YELLOW, -1);
    value = system_check (eSYSTEM_MEM);
    memset (str, 0, sizeof(str));   sprintf(str, "%d GB", value);

    ui_set_sitem (p->pfb, p->pui, UI_MEM, -1, -1, str);
    ui_set_ritem (p->pfb, p->pui, UI_MEM, value ? COLOR_GREEN : COLOR_RED, -1);
    ItemStatus[eITEM_MEM] = value ? eSTATUS_PASS : eSTATUS_FAIL;

    // FB
    ui_set_ritem (p->pfb, p->pui, UI_FB, COLOR_YELLOW, -1);
    value = system_check (eSYSTEM_FB_Y);
    memset (str, 0, sizeof(str));   sprintf(str, "%dP", value);

    ui_set_sitem (p->pfb, p->pui, UI_FB, -1, -1, str);
    ui_set_ritem (p->pfb, p->pui, UI_FB, (value == 1080) ? COLOR_GREEN : COLOR_RED, -1);
    ItemStatus[eITEM_FB] = (value == 1080) ? eSTATUS_PASS : eSTATUS_FAIL;

    return 1;
}

//------------------------------------------------------------------------------
#define UI_EDID     53
#define UI_HPD      54

static int check_device_hdmi (client_t *p)
{
    int value = 0;

    // EDID
    ui_set_ritem (p->pfb, p->pui, UI_EDID, COLOR_YELLOW, -1);
    value = hdmi_check (eHDMI_EDID);
    ui_set_sitem (p->pfb, p->pui, UI_EDID, -1, -1, value ? "PASS":"FAIL");
    ui_set_ritem (p->pfb, p->pui, UI_EDID, value ? COLOR_GREEN : COLOR_RED, -1);
    ItemStatus[eITEM_EDID] = value ? eSTATUS_PASS : eSTATUS_FAIL;

    // HPD
    ui_set_ritem (p->pfb, p->pui, UI_HPD, COLOR_YELLOW, -1);
    value = adc_check (eHDMI_HPD);
    ui_set_sitem (p->pfb, p->pui, UI_HPD, -1, -1, value ? "PASS":"FAIL");
    ui_set_ritem (p->pfb, p->pui, UI_HPD, value ? COLOR_GREEN : COLOR_RED, -1);
    ItemStatus[eITEM_HPD] = value ? eSTATUS_PASS : eSTATUS_FAIL;

    return 1;
}

//------------------------------------------------------------------------------
#define UI_ADC37    192
#define UI_ADC40    193

static int check_device_adc (client_t *p)
{
    int adc_value = 0;
    char str[10];

    // ADC37
    ui_set_ritem (p->pfb, p->pui, UI_ADC37, COLOR_YELLOW, -1);
    adc_value = adc_check (eADC_H37);
    memset  (str, 0, sizeof(str));  sprintf (str, "%d", adc_value);
    ui_set_sitem (p->pfb, p->pui, UI_ADC37, -1, -1, str);
    ui_set_ritem (p->pfb, p->pui, UI_ADC37, adc_value ? COLOR_GREEN : COLOR_RED, -1);
    ItemStatus[eITEM_ADC37] = adc_value ? eSTATUS_PASS : eSTATUS_FAIL;

    // ADC40
    ui_set_ritem (p->pfb, p->pui, UI_ADC40, COLOR_YELLOW, -1);
    adc_value = adc_check (eADC_H40);
    memset  (str, 0, sizeof(str));  sprintf (str, "%d", adc_value);
    ui_set_sitem (p->pfb, p->pui, UI_ADC40, -1, -1, str);
    ui_set_ritem (p->pfb, p->pui, UI_ADC40, adc_value ? COLOR_GREEN : COLOR_RED, -1);
    ItemStatus[eITEM_ADC40] = adc_value ? eSTATUS_PASS : eSTATUS_FAIL;

    return 1;
}

//------------------------------------------------------------------------------
#define UI_MAC_ADDR 147

static int check_mac_addr (client_t *p)
{
    int ui_id = 0;
    char str[32];

    ItemStatus[eITEM_IPERF] = eSTATUS_FAIL;
    efuse_set_board (eBOARD_ID_M2);

    ui_id = UI_MAC_ADDR;
    ui_set_ritem (p->pfb, p->pui, ui_id, COLOR_YELLOW, -1);

    if (efuse_control (p->efuse_data, EFUSE_READ)) {
        efuse_get_mac (p->efuse_data, p->mac);
        if (!efuse_valid_check (p->efuse_data)) {
            if (mac_server_request (MAC_SERVER_FACTORY, REQ_TYPE_UUID, "m2",
                                    p->efuse_data)) {
                if (efuse_control (p->efuse_data, EFUSE_WRITE)) {
                    efuse_get_mac (p->efuse_data, p->mac);
                   if (efuse_valid_check (p->efuse_data))
                        ItemStatus[eITEM_MAC_ADDR] = eSTATUS_PASS;
                }
            }
        } else {
            ItemStatus[eITEM_MAC_ADDR] = eSTATUS_PASS;
        }
    }

    memset (str, 0, sizeof(str));
    sprintf(str, "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
            p->mac[0],  p->mac[1], p->mac[2],
            p->mac[3],  p->mac[4], p->mac[5],
            p->mac[6],  p->mac[7], p->mac[8],
            p->mac[9], p->mac[10], p->mac[11]);

    ui_set_sitem (p->pfb, p->pui, ui_id, -1, -1, str);

    if (ItemStatus[eITEM_MAC_ADDR] == eSTATUS_PASS) {
        ui_set_ritem (p->pfb, p->pui, ui_id, COLOR_GREEN, -1);
        tolowerstr (p->mac);
        nlp_server_write (p->nlp_ip, NLP_SERVER_MSG_TYPE_MAC, p->mac, NLP_SERVER_CHANNEL_LEFT);
        return 1;
    }
    ui_set_ritem (p->pfb, p->pui, ui_id, COLOR_RED, -1);
    return 0;
}

//------------------------------------------------------------------------------
#define UI_IPERF        127
#define IPERF_SPEED_MIN 800

static int check_iperf_speed (client_t *p)
{
    int value = 0, ui_id = 0;
    char str[32];

    ui_id = UI_IPERF;
    ui_set_ritem (p->pfb, p->pui, ui_id, COLOR_YELLOW, -1);
    nlp_server_write (p->nlp_ip, NLP_SERVER_MSG_TYPE_UDP, "start", 0);  sleep (1);
    value = iperf3_speed_check(p->nlp_ip, NLP_SERVER_MSG_TYPE_UDP);
    nlp_server_write (p->nlp_ip, NLP_SERVER_MSG_TYPE_UDP, "stop", 0);   sleep (1);

    memset  (str, 0, sizeof(str));
    sprintf (str, "%d Mbits/sec", value);

    ui_set_sitem (p->pfb, p->pui, ui_id, -1, -1, str);
    ui_set_ritem (p->pfb, p->pui, ui_id, value > IPERF_SPEED_MIN ? COLOR_GREEN : COLOR_RED, -1);
    ItemStatus[eITEM_IPERF] = value > IPERF_SPEED_MIN ? eSTATUS_PASS : eSTATUS_FAIL;

    return 1;
}

//------------------------------------------------------------------------------
#define UI_BOARD_IP     4
#define UI_SERVER_IP    24

static int check_server (client_t *p)
{
    char ip_addr [IP_ADDR_SIZE];
    int ui_id = 0;

    memset (ip_addr, 0, sizeof(ip_addr));

    ui_id = UI_BOARD_IP;
    ui_set_ritem (p->pfb, p->pui, ui_id, COLOR_YELLOW, -1);
    if (get_my_ip (ip_addr)) {
        ui_set_sitem (p->pfb, p->pui, ui_id, -1, -1, ip_addr);
        ui_set_ritem (p->pfb, p->pui, ui_id, p->pui->bc.uint, -1);

        memset (ip_addr, 0, sizeof(ip_addr));

        ui_id = UI_SERVER_IP;
        ui_set_ritem (p->pfb, p->pui, ui_id, COLOR_YELLOW, -1);
        if (nlp_server_find(ip_addr)) {
            memcpy (p->nlp_ip, ip_addr, IP_ADDR_SIZE);
            ui_set_sitem (p->pfb, p->pui, ui_id, -1, -1, ip_addr);
            ui_set_ritem (p->pfb, p->pui, ui_id, p->pui->bc.uint, -1);
            ItemStatus[eITEM_SERVER] = eSTATUS_PASS;
            return 1;
        } else {
            ui_set_ritem (p->pfb, p->pui, ui_id, COLOR_RED, -1);
        }
    } else {
        ui_set_ritem (p->pfb, p->pui, ui_id, COLOR_RED, -1);
    }

    ItemStatus[eITEM_SERVER] = eSTATUS_FAIL;
    return 0;
}

//------------------------------------------------------------------------------
static int client_setup (client_t *p)
{
	pthread_t thread_hp_detect, thread_sw_adc, thread_check_server, thread_ethernet;

    if ((p->pfb = fb_init (DEVICE_FB)) == NULL)         exit(1);
    if ((p->pui = ui_init (p->pfb, CONFIG_UI)) == NULL) exit(1);
//void *check_status (void *arg)

    pthread_create (&thread_check_server, NULL, check_status, p);
    pthread_create (&thread_hp_detect, NULL, check_hp_detect, p);
    pthread_create (&thread_sw_adc,    NULL, check_sw_adc, p);

    ethernet_link_setup (LINK_SPEED_1G);    sleep(1);

    check_server (p);
    check_iperf_speed (p);
    check_mac_addr (p);
    check_device_adc (p);
    check_device_hdmi(p);
    check_device_system (p);
    check_device_storage (p);
    check_device_usb (p);

    pthread_create (&thread_ethernet,  NULL, check_device_ethernet, p);

    return 1;
}

//------------------------------------------------------------------------------
static void client_alive_display (client_t *p)
{
    static struct timeval i_time;
    static int onoff = 0;

    if (run_interval_check(&i_time, ALIVE_DISPLAY_INTERVAL)) {
        ui_set_ritem (p->pfb, p->pui, ALIVE_DISPLAY_UI_ID,
                    onoff ? COLOR_GREEN : p->pui->bc.uint, -1);
        onoff = !onoff;

        if (onoff)  ui_update (p->pfb, p->pui, -1);
    }
}
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
int main (void)
{
    client_t client;

    memset (&client, 0, sizeof(client));

    // UI
    client_setup (&client);
#if 0
    // client device init (lib_dev_check)
    device_setup ();

    // Dispaly device init data
    client_init_data (&client);
    // Get Network printer info
    if (nlp_init (&client.nlp, NULL)) {
        char mac_str[20];

        memset (mac_str, 0, sizeof(mac_str));
        ethernet_mac_str (mac_str);
        nlp_printf (&client.nlp, MSG_TYPE_MAC, mac_str, CH_NONE);
    }
#endif

    while (1) {
//        client_alive_display (&client);

//        usleep (APP_LOOP_DELAY);
    }
    return 0;
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
