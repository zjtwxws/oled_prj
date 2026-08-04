#pragma once

#define IDR_MAINFRAME           128

// Dialog
#define IDD_OLED_CONTROL_DIALOG 101

// Controls
#define IDC_COMBO_PORT          1001
#define IDC_BTN_OPEN            1002
#define IDC_BTN_CLOSE           1003
#define IDC_STC_CONN_STATUS     1004
#define IDC_RADIO_REMOTE       1005  /* v3.1: 远程模式 */
#define IDC_RADIO_LOCAL        1006  /* v3.1: 本地模式 */
#define IDC_CBO_REMOTE_SUB     1007  /* v3.1: 远程子模式 */
#define IDC_RADIO_LED_OFF       1010
#define IDC_RADIO_LED_ON        1011
#define IDC_RADIO_LED_BLINK     1012
#define IDC_RADIO_MODE_BASE     1020
#define IDC_EDIT_TEXT           1030
#define IDC_BTN_SEND_TEXT       1031
#define IDC_EDIT_BOOT_TEXT      1032
#define IDC_BTN_SAVE_BOOT       1033
#define IDC_CBO_WEATHER_TYPE    1040
#define IDC_EDIT_TEMP           1041
#define IDC_EDIT_HUMIDITY       1042
#define IDC_CBO_WIND            1043
#define IDC_BTN_SEND_WEATHER    1044
#define IDC_BTN_SYNC_TIME       1050
#define IDC_STC_LED_STATUS      1060
#define IDC_STC_MODE_STATUS     1061
#define IDC_STC_LATENCY         1062
#define IDC_GRP_DEVICE_STATUS   1063
#define IDC_LST_KEY_LOG         1070
#define IDC_GRP_KEY_LOG         1071
#define IDC_GRP_WEATHER         1072  /* v3.2: 天气模拟分组框 */
#define IDC_GRP_TEXT            1073  /* v3.2: 文字内容分组框 */
#define IDC_GRP_SAVE_BOOT       1074  /* v3.2: 上电默认文字分组框 */
#define IDC_STC_WTH_TYPE        1075  /* v3.2: 天气"类型:"标签 */
#define IDC_STC_WTH_TEMP        1076  /* v3.2: 天气"温度:"标签 */
#define IDC_STC_WTH_HUMID       1077  /* v3.2: 天气"湿度:"标签 */
#define IDC_STC_WTH_WIND        1078  /* v3.2: 天气"风向:"标签 */
#define IDC_STC_WTH_CELSIUS     1079  /* v3.2: 天气"℃"标签 */
#define IDC_STC_WTH_PERCENT     1080  /* v3.2: 天气"%"标签 */
#define IDC_OLED_PREVIEW        1081
#define IDC_STC_REMOTE_MODE     1085  /* v3.1: 远程子模式文本标签 */
#define IDC_STATUS_BAR          1090  /* v3.2: 底部状态栏 */

// Next default values for new objects
#ifdef APSTUDIO_INVOKED
#ifndef APSTUDIO_READONLY_SYMBOLS
#define _APS_NEXT_RESOURCE_VALUE 130
#define _APS_NEXT_COMMAND_VALUE  32771
#define _APS_NEXT_CONTROL_VALUE  1100
#define _APS_NEXT_SYMED_VALUE    110
#endif
#endif
