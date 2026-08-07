/**
 * @file    cli_cmds.h
 * @brief   CLI 调试命令注册接口
 */

#ifndef __CLI_CMDS_H
#define __CLI_CMDS_H

/**
 * @brief  注册所有自定义 CLI 命令到 cmd_table
 * @note   在 cli_init() 之后调用一次即可；cmd_table 是全局表，无需重复注册
 */
void cli_cmds_init(void);

#endif /* __CLI_CMDS_H */
