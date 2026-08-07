# 1. Add sys_config_reset() to sys_config.h
hpath = r'E:\BaiduNetdiskDownload\code\oled_prj\stm32f407\inc\sys_config.h'
with open(hpath, 'r', encoding='utf-8') as f:
    c = f.read()
c = c.replace(
    '#endif /* __SYS_CONFIG_H */',
    '/** @brief \xe7\xb3\xbb\xe7\xbb\x9f\xe5\xa4\x8d\xe4\xbd\x8d (\xe8\xb0\x83\xe7\x94\xa8 NVIC_SystemReset\xef\xbc\x8c\xe4\xb8\x8d\xe8\xbf\x94\xe5\x9b\x9e) */\nvoid sys_config_reset(void);\n\n#endif /* __SYS_CONFIG_H */'
)
with open(hpath, 'w', encoding='utf-8') as f:
    f.write(c)
print('sys_config.h OK')

# 2. Add sys_config_reset() to sys_config.c
cpath = r'E:\BaiduNetdiskDownload\code\oled_prj\stm32f407\src\sys_config.c'
with open(cpath, 'r', encoding='utf-8') as f:
    c = f.read()
c = c.replace(
    'uint8_t sys_config_get_poweron_type(void)',
    'void sys_config_reset(void)\n{\n    NVIC_SystemReset();\n}\n\nuint8_t sys_config_get_poweron_type(void)'
)
with open(cpath, 'w', encoding='utf-8') as f:
    f.write(c)
print('sys_config.c OK')

# 3. Fix menu_items.c: remove HAL include, use sys_config_reset()
mpath = r'E:\BaiduNetdiskDownload\code\oled_prj\stm32f407\src\menu_items.c'
with open(mpath, 'r', encoding='utf-8') as f:
    c = f.read()
c = c.replace('#include "stm32f4xx_hal.h"\n', '')
c = c.replace('NVIC_SystemReset();', 'sys_config_reset();')
with open(mpath, 'w', encoding='utf-8') as f:
    f.write(c)
print('menu_items.c OK')

print('ALL DONE')