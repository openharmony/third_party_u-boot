/*
 * Copyright (c) 2020 HiSilicon (Shanghai) Technologies CO., LIMITED.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "mipi_tx.h"
#include <common.h>
#include "type.h"
#include "mipi_tx_hal.h"
#include "hi3516cv500_vo.h"

#define MIPI_TX_DEV_NAME  "hi_mipi_tx"
#define MIPI_TX_PROC_NAME "mipi_tx"

#define HIMEDIA_DYNAMIC_MINOR 255

#define REGFLAG_DELAY               0XFFE
#define REGFLAG_END_OF_TABLE        0xFFF // END OF REGISTERS MARKER
#define DTYPE_DCS_WRITE             0x5 // 0x15 short write, 1 parameter
#define DTYPE_DCS_WRITE1            0x15 // 0x23  short write, 2 parameter
#define DTYPE_DCS_LWRITE            0x39 // 0x29 long write

extern void pwm_mipi_lcm();

typedef struct {
    combo_dev_cfg_t dev_cfg;
} mipi_tx_dev_ctx_t;

struct LCM_setting_table {
    unsigned cmd;
    unsigned char count;
    unsigned char para_list[100];
};

static struct LCM_setting_table lcm_initialization_setting[] = {
    {DTYPE_DCS_LWRITE, 3, {0xF0, 0x5A, 0x5A}},
    {DTYPE_DCS_LWRITE, 3, {0xF1, 0xA5, 0xA5}},

    {DTYPE_DCS_LWRITE, 12, {0xB3, 0x03, 0x03, 0x03, 0x07, 0x05, 0x0D, 0x0F, 0x11, 0x13, 0x09, 0x0B}},
    {DTYPE_DCS_LWRITE, 12, {0xB4, 0x03, 0x03, 0x03, 0x06, 0x04, 0x0C, 0x0E, 0x10, 0x12, 0x08, 0x0A}},
    {DTYPE_DCS_LWRITE, 13, {0xB0, 0x54, 0x32, 0x23, 0x45, 0x44, 0x44, 0x44, 0x44, 0x60, 0x00, 0x60, 0x1C}},
    {DTYPE_DCS_LWRITE, 9, {0xB1, 0x32, 0x84, 0x02, 0x87, 0x12, 0x00, 0x50, 0x1C}},
    {DTYPE_DCS_LWRITE, 4, {0xB2, 0x73, 0x09, 0x08}},

    {DTYPE_DCS_LWRITE, 4, {0xB6, 0x5C, 0x5C, 0x05}},
    // {DTYPE_DCS_WRITE1, 2,  {0xC0, 0x11}}, //BIST color bar and color test disable

    {DTYPE_DCS_LWRITE, 6,  {0xB8, 0x23, 0x41, 0x32, 0x30, 0x03}},
    {DTYPE_DCS_LWRITE, 11, {0xBC, 0xD2, 0x0E, 0x63, 0x63, 0x5A, 0x32, 0x22, 0x14, 0x22, 0x03}},
    {DTYPE_DCS_WRITE1, 2, {0xB7, 0x41}},
    {DTYPE_DCS_LWRITE, 7, {0xC1, 0x0C, 0x10, 0x04, 0x0C, 0x10, 0x04}},
    {DTYPE_DCS_LWRITE, 3, {0xC2, 0x10, 0xE0}},
    {DTYPE_DCS_LWRITE, 3, {0xC3, 0x22, 0x11}},

    {DTYPE_DCS_LWRITE, 3, {0xD0, 0x07, 0xFF}},
    {DTYPE_DCS_LWRITE, 5, {0xD2, 0x63,0x0B,0x08,0x88}}, //ESD

    {DTYPE_DCS_LWRITE, 8, {0xC6, 0x08, 0x15, 0xFF, 0x10, 0x16, 0x80, 0x60}},
    {DTYPE_DCS_WRITE1, 2, {0xC7, 0x04}},

    {DTYPE_DCS_LWRITE, 39, {0xC8, 0x7C, 0x50, 0x3B, 0x2C, 0x25, 0x16, 0x1C, \
                            0x08, 0x27, 0x2B, 0x2F, 0x52, 0x43, 0x4C, 0x40, \
                            0x3D, 0x30, 0x1E, 0x06, 0x7C, 0x50, 0x3B, 0x2C, \
                            0x25, 0x16, 0x1C, 0x08, 0x27, 0x2B, 0x2F, 0x52, \
                            0x43, 0x4C, 0x40, 0x3D, 0x30, 0x1E, 0x06}}, // G2.0

    {DTYPE_DCS_WRITE, 1, {0x11}}, // exit sleep
    {REGFLAG_DELAY, 0, {}},
    {DTYPE_DCS_WRITE, 1, {0x29}}, // display on
    {REGFLAG_DELAY,0,{}},
    {REGFLAG_END_OF_TABLE, 0x00,{}}
};

mipi_tx_dev_ctx_t g_mipi_tx_dev_ctx;

/* the numbers below is the initialization of the dev config, not magic number */
/* 720x576_50 sync config */
static combo_dev_cfg_t g_mipi_tx_720x576_50_config = {
    .devno = 0,
    .lane_id = { 0, 1, 2, 3 },
    .output_mode = OUTPUT_MODE_DSI_VIDEO,
    .output_format = OUT_FORMAT_RGB_24_BIT,
    .video_mode = BURST_MODE,
    .sync_info = {
        .vid_pkt_size = 720,
        .vid_hsa_pixels = 64,
        .vid_hbp_pixels = 68,
        .vid_hline_pixels = 864,
        .vid_vsa_lines = 5,
        .vid_vbp_lines = 39,
        .vid_vfp_lines = 5,
        .vid_active_lines = 576,
        .edpi_cmd_size = 0,
    },
    .phy_data_rate = 459,
    .pixel_clk = 27000,
};

/* 480x960_60 sync config */
static combo_dev_cfg_t g_mipi_tx_480x960_60_config = {
    .devno = 0,
    .lane_id = { 0, 1, -1, -1 },
    .output_mode = OUTPUT_MODE_DSI_VIDEO, //OUTPUT_MODE_DSI_CMD,
    .output_format = OUT_FORMAT_RGB_24_BIT,
    .video_mode = BURST_MODE,
    .sync_info = {
        .vid_pkt_size = 480, // hact
        .vid_hsa_pixels = 10, // hsa
        .vid_hbp_pixels = 20, // hbp
        .vid_hline_pixels = 530, // hact(480) + hsa(10) + hbp(20) + hfp(20)
        .vid_vsa_lines = 2, // vsa
        .vid_vbp_lines = 14, // vbp
        .vid_vfp_lines = 16, // vfp
        .vid_active_lines = 960, // vact
        .edpi_cmd_size = 0,
    },
    .phy_data_rate = 379,
    .pixel_clk = 31546,
};

/* 1280x720_60 sync config */
static combo_dev_cfg_t g_mipi_tx_1280x720_60_config = {
    .devno = 0,
    .lane_id = { 0, 1, 2, 3 },
    .output_mode = OUTPUT_MODE_DSI_VIDEO,
    .output_format = OUT_FORMAT_RGB_24_BIT,
    .video_mode = BURST_MODE,
    .sync_info = {
        .vid_pkt_size = 1280,
        .vid_hsa_pixels = 40,
        .vid_hbp_pixels = 220,
        .vid_hline_pixels = 1650,
        .vid_vsa_lines = 5,
        .vid_vbp_lines = 20,
        .vid_vfp_lines = 5,
        .vid_active_lines = 720,
        .edpi_cmd_size = 0,
    },
    .phy_data_rate = 459,
    .pixel_clk = 74250,
};

/* 1920x1080_60 sync config */
static combo_dev_cfg_t g_mipi_tx_1920x1080_60_config = {
    .devno = 0,
    .lane_id = { 0, 1, 2, 3 },
    .output_mode = OUTPUT_MODE_DSI_VIDEO,
    .output_format = OUT_FORMAT_RGB_24_BIT,
    .video_mode = BURST_MODE,
    .sync_info = {
        .vid_pkt_size = 1920,
        .vid_hsa_pixels = 44,
        .vid_hbp_pixels = 148,
        .vid_hline_pixels = 2200,
        .vid_vsa_lines = 5,
        .vid_vbp_lines = 36,
        .vid_vfp_lines = 4,
        .vid_active_lines = 1080,
        .edpi_cmd_size = 0,
    },
    .phy_data_rate = 945,
    .pixel_clk = 148500,
};

/* 1024x768_60 sync config */
static combo_dev_cfg_t g_mipi_tx_1024x768_60_config = {
    .devno = 0,
    .lane_id = { 0, 1, 2, 3 },
    .output_mode = OUTPUT_MODE_DSI_VIDEO,
    .output_format = OUT_FORMAT_RGB_24_BIT,
    .video_mode = BURST_MODE,
    .sync_info = {
        .vid_pkt_size = 1024,
        .vid_hsa_pixels = 136,
        .vid_hbp_pixels = 160,
        .vid_hline_pixels = 1344,
        .vid_vsa_lines = 6,
        .vid_vbp_lines = 29,
        .vid_vfp_lines = 3,
        .vid_active_lines = 768,
        .edpi_cmd_size = 0,
    },
    .phy_data_rate = 495,  /* 486 */
    .pixel_clk = 65000,
};

/* 1280x1024_60 sync config */
static combo_dev_cfg_t g_mipi_tx_1280x1024_60_config = {
    .devno = 0,
    .lane_id = { 0, 1, 2, 3 },
    .output_mode = OUTPUT_MODE_DSI_VIDEO,
    .output_format = OUT_FORMAT_RGB_24_BIT,
    .video_mode = BURST_MODE,
    .sync_info = {
        .vid_pkt_size = 1280,
        .vid_hsa_pixels = 112,
        .vid_hbp_pixels = 248,
        .vid_hline_pixels = 1688,
        .vid_vsa_lines = 3,
        .vid_vbp_lines = 38,
        .vid_vfp_lines = 1,
        .vid_active_lines = 1024,
        .edpi_cmd_size = 0,
    },
    .phy_data_rate = 495,  /* 486 */
    .pixel_clk = 108000,
};

/* 720x1280_60 sync config */
static combo_dev_cfg_t g_mipi_tx_720x1280_60_config = {
    .devno = 0,
    .lane_id = { 0, 1, 2, 3 },
    .output_mode = OUTPUT_MODE_DSI_VIDEO,
    .output_format = OUT_FORMAT_RGB_24_BIT,
    .video_mode = BURST_MODE,
    .sync_info = {
        .vid_pkt_size = 720,       /* hact */
        .vid_hsa_pixels = 24,      /* hsa */
        .vid_hbp_pixels = 99,      /* hbp */
        .vid_hline_pixels = 943,   /* hact + hsa + hbp + hfp */
        .vid_vsa_lines = 4,        /* vsa */
        .vid_vbp_lines = 20,       /* vbp */
        .vid_vfp_lines = 8,        /* vfp */
        .vid_active_lines = 1280,  /* vact */
        .edpi_cmd_size = 0,
    },
    .phy_data_rate = 459,
    .pixel_clk = 74250,
};

/* 1080x1920_60 sync config */
static combo_dev_cfg_t g_mipi_tx_1080x1920_60_config = {
    .devno = 0,
    .lane_id = { 0, 1, 2, 3 },
    .output_mode = OUTPUT_MODE_DSI_VIDEO,
    .output_format = OUT_FORMAT_RGB_24_BIT,
    .video_mode = BURST_MODE,
    .sync_info = {
        .vid_pkt_size = 1080,
        .vid_hsa_pixels = 8,
        .vid_hbp_pixels = 20,
        .vid_hline_pixels = 1238,
        .vid_vsa_lines = 10,
        .vid_vbp_lines = 26,
        .vid_vfp_lines = 16,
        .vid_active_lines = 1920,
        .edpi_cmd_size = 0,
    },
    .phy_data_rate = 945,
    .pixel_clk = 148500,
};


static int mipi_tx_check_comb_dev_cfg(const combo_dev_cfg_t *dev_cfg)
{
    return 0;
}

static int mipi_tx_set_combo_dev_cfg(const combo_dev_cfg_t *dev_cfg)
{
    int ret;

    ret = mipi_tx_check_comb_dev_cfg(dev_cfg);
    if (ret < 0) {
        hi_err("mipi_tx check combo_dev config failed!\n");
        return -1;
    }

    /* set controler config */
    mipi_tx_drv_set_controller_cfg(dev_cfg);

    /* set phy config */
    mipi_tx_drv_set_phy_cfg(dev_cfg);

    memcpy(&g_mipi_tx_dev_ctx.dev_cfg, dev_cfg, sizeof(combo_dev_cfg_t));

    return ret;
}

static int mipi_tx_set_cmd(const cmd_info_t *cmd_info)
{
    return mipi_tx_drv_set_cmd_info(cmd_info);
}

static int mipi_tx_get_cmd(get_cmd_info_t *get_cmd_info)
{
    return mipi_tx_drv_get_cmd_info(get_cmd_info);
}

static void mipi_tx_enable(void)
{
    output_mode_t output_mode;

    output_mode = g_mipi_tx_dev_ctx.dev_cfg.output_mode;

    mipi_tx_drv_enable_input(output_mode);
}

static void mipi_tx_disable(void)
{
    mipi_tx_drv_disable_input();
}

static long mipi_tx_ioctl(unsigned int cmd, unsigned long arg)
{
    int ret = 0;

    switch (cmd) {
        case HI_MIPI_TX_SET_DEV_CFG: {
            combo_dev_cfg_t *combo_dev_cfg = (combo_dev_cfg_t *)(uintptr_t)arg;
            mipi_tx_check_null_ptr_return(combo_dev_cfg);

            ret = mipi_tx_set_combo_dev_cfg(combo_dev_cfg);
            if (ret < 0) {
                hi_err("mipi_tx set combo_dev config failed!\n");
                ret = -1;
            }
            break;
        }

        case HI_MIPI_TX_SET_CMD: {
            cmd_info_t *cmd_info = (cmd_info_t *)(uintptr_t)arg;
            mipi_tx_check_null_ptr_return(cmd_info);

            ret = mipi_tx_set_cmd(cmd_info);
            if (ret < 0) {
                hi_err("mipi_tx set cmd failed!\n");
                ret = -1;
            }
            break;
        }

        case HI_MIPI_TX_GET_CMD: {
            get_cmd_info_t *get_cmd_info = (get_cmd_info_t *)(uintptr_t)arg;
            mipi_tx_check_null_ptr_return(get_cmd_info);
            ret = mipi_tx_get_cmd(get_cmd_info);
            if (ret < 0) {
                hi_err("mipi_tx get cmd failed!\n");
                ret = -1;
            }
            break;
        }

        case HI_MIPI_TX_ENABLE: {
            mipi_tx_enable();
            break;
        }

        case HI_MIPI_TX_DISABLE: {
            mipi_tx_disable();
            break;
        }

        default: {
            hi_err("invalid mipi_tx ioctl cmd\n");
            ret = -1;
            break;
        }
    }

    return ret;
}

static int mipi_tx_init(void)
{
    return mipi_tx_drv_init();
}

static void mipi_tx_exit(void)
{
    mipi_tx_drv_exit();
}

int mipi_tx_module_init(void)
{
    int ret;

    ret = mipi_tx_init();
    if (ret != 0) {
        printf("hi_mipi_init failed!\n");
        return -1;
    }

    printf("load mipi_tx driver successful!\n");
    return 0;
}

void mipi_tx_module_exit(void)
{
    mipi_tx_exit();

    printf("unload mipi_tx driver ok!\n");
}

static void push_table(struct LCM_setting_table *table, unsigned int count, unsigned char force_update)
{
    unsigned int i;
    int s32Ret;

    for(i = 0; i < count; i++) {
        unsigned cmd;
        cmd = table[i].cmd;
        cmd_info_t cmd_info = {0};

        switch (cmd) {
            case REGFLAG_DELAY :
                 mdelay(120);
                break;
            case REGFLAG_END_OF_TABLE :
                break;
            default:
                if (cmd == DTYPE_DCS_LWRITE) {
                    cmd_info.devno = 0;
                    cmd_info.cmd_size = table[i].count;
                    cmd_info.data_type = table[i].cmd;
                    cmd_info.cmd = &table[i].para_list[0];
                    s32Ret = mipi_tx_ioctl(HI_MIPI_TX_SET_CMD, (unsigned long)(&cmd_info));
                    mdelay(1);
                } else if (cmd == DTYPE_DCS_WRITE1) {
                    cmd_info.devno = 0;
                    cmd_info.cmd_size |= table[i].para_list[1] << 8;
                    cmd_info.cmd_size |= table[i].para_list[0];
                    cmd_info.data_type = table[i].cmd;
                    cmd_info.cmd = NULL;
                    s32Ret = mipi_tx_ioctl(HI_MIPI_TX_SET_CMD, (unsigned long)(&cmd_info));
                    mdelay(1);
                } else if (cmd == DTYPE_DCS_WRITE) {
                    cmd_info.devno = 0;
                    cmd_info.cmd_size = table[i].para_list[0];
                    cmd_info.data_type = table[i].cmd;
                    cmd_info.cmd = NULL;
                    s32Ret = mipi_tx_ioctl(HI_MIPI_TX_SET_CMD, (unsigned long)(&cmd_info));
                    mdelay(1);
                }
        }
    }
}

static void PLE_PRIVATE_VO_InitScreen480x960(void)
{
    printf("Send mipi cmd \n");
    push_table(lcm_initialization_setting, sizeof(lcm_initialization_setting) / sizeof(struct LCM_setting_table), 1);
}

int mipi_tx_display(unsigned int vosync)
{
    int ret;
    combo_dev_cfg_t *mipi_tx_config = HI_NULL;
    printf("this is mipi_tx begin.\n");
    /* mipi_tx drv init. */
    mipi_tx_module_init();

    switch (vosync) {
        case VO_OUTPUT_480x960_60:
            mipi_tx_config = &g_mipi_tx_480x960_60_config;
            break;
        case VO_OUTPUT_576P50:
            mipi_tx_config = &g_mipi_tx_720x576_50_config;
            break;
        case VO_OUTPUT_720P60:
            mipi_tx_config = &g_mipi_tx_1280x720_60_config;
            break;
        case VO_OUTPUT_1080P60:
            mipi_tx_config = &g_mipi_tx_1920x1080_60_config;
            break;
        case VO_OUTPUT_1024x768_60:
            mipi_tx_config = &g_mipi_tx_1024x768_60_config;
            break;
        case VO_OUTPUT_1280x1024_60:
            mipi_tx_config = &g_mipi_tx_1280x1024_60_config;
            break;
        case VO_OUTPUT_720x1280_60:
            mipi_tx_config = &g_mipi_tx_720x1280_60_config;
            break;
        case VO_OUTPUT_1080x1920_60:
            mipi_tx_config = &g_mipi_tx_1080x1920_60_config;
            break;
        default:
            mipi_tx_config = &g_mipi_tx_1920x1080_60_config;
            break;
    }

    /* step 1 : config mipi_tx controller. */
    ret = mipi_tx_ioctl(HI_MIPI_TX_SET_DEV_CFG, (unsigned long)(uintptr_t)mipi_tx_config);
    if (ret != 0) {
        printf("MIPI_TX SET_DEV_CONFIG failed\n");
        return -1;
    }

    /* step 2 : init display device (do it yourself ). */
    udelay(10000); /* delay 10000 us for the stable signal */
    PLE_PRIVATE_VO_InitScreen480x960();
    udelay(10000);
    pwm_mipi_lcm();

    /* step 3 : enable mipi_tx controller. */
    ret = mipi_tx_ioctl(HI_MIPI_TX_ENABLE, (unsigned long)0);
    if (ret != 0) {
        printf("MIPI_TX enable failed\n");
        return -1;
    }

    printf("this is mipi_tx end.\n");

    return 0;
}

int mipi_tx_stop(void)
{
    printf("this is mipi_tx stop begin.\n");

    /* mipi_tx drv exit. */
    mipi_tx_module_exit();
    printf("this is mipi_tx stop end.\n");
    return 0;
}

