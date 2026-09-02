/**
 * @file    cli_cmds.h
 * @brief   CLI 调试命令注册接口
 */

#ifndef __CLI_CMDS_H
#define __CLI_CMDS_H

#include <stdint.h>

/**
 * @brief  CLI 命令处理函数指针类型
 * @param  argc 参数个数
 * @param  argv 参数列表
 * @return 0 表示成功，非 0 表示失败
 */
typedef int (*fn_cli_cmd_t)(uint8_t argc, char **argv);

/**
 * @brief  注册所有自定义 CLI 命令到 cmd_table
 * @note   在 cli_init() 之后调用一次即可；cmd_table 是全局表，无需重复注册
 */
void cli_cmds_init(void);
int cli_cmd_register(char *name, fn_cli_cmd_t pfunc, char *desc);
int cli_cmds_coplete_words_register(char *words[], uint16_t size);
void cli_task(void *argument);

#endif /* __CLI_CMDS_H */
