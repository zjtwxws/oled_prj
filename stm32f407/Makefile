# STM32F407 Makefile (GNU Arm Embedded Toolchain)
# 需配合 CubeMX 生成的代码使用

TARGET = oled_f407
BUILD_DIR = build
CORE_DIR = Core

# 工具链
CC      = arm-none-eabi-gcc
CXX     = arm-none-eabi-g++
AS      = arm-none-eabi-as
LD      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

# MCU 参数
CPU     = -mcpu=cortex-m4
FPU     = -mfpu=fpv4-sp-d16 -mfloat-abi=hard
MCU     = $(CPU) -mthumb $(FPU)

# 源文件 (项目模块)
PROJ_SRC = src/ssd1306.c src/font.c src/display_mgr.c \
           src/i2c_drv.c src/uart_drv.c src/protocol.c \
           src/led_mgr.c src/key_drv.c src/iwdg_drv.c src/sys_config.c \
           src/debug_console.c \
           src/main.c

# CubeMX 生成的 HAL 源文件 (需用户补充路径)
HAL_SRC  = $(CORE_DIR)/Src/main.c $(CORE_DIR)/Src/stm32f4xx_it.c \
           $(CORE_DIR)/Src/stm32f4xx_hal_msp.c $(CORE_DIR)/Src/system_stm32f4xx.c

# HAL 库文件
HAL_LIB  = Drivers/STM32F4xx_HAL_Driver/Src/*.c

# 启动文件
STARTUP  = startup_stm32f407xx.s

# 编译标志
CFLAGS   = $(MCU) -Wall -O2 -g
CFLAGS  += -DSTM32F407xx -DUSE_HAL_DRIVER
CFLAGS  += -Iinc -I$(CORE_DIR)/Inc -IDrivers/STM32F4xx_HAL_Driver/Inc -IDrivers/CMSIS/Device/ST/STM32F4xx/Include -IDrivers/CMSIS/Include

LDFLAGS  = $(MCU) -TSTM32F407VGTx_FLASH.ld -Wl,--gc-sections -specs=nosys.specs -specs=nano.specs -u _printf_float

all: $(TARGET).elf $(TARGET).bin $(TARGET).hex

$(TARGET).elf: $(PROJ_SRC) $(HAL_SRC) $(HAL_LIB) $(STARTUP)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(PROJ_SRC) $(HAL_SRC) $(HAL_LIB) $(STARTUP) -o $(BUILD_DIR)/$@
	$(SIZE) $(BUILD_DIR)/$@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $(BUILD_DIR)/$< $(BUILD_DIR)/$@

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex $(BUILD_DIR)/$< $(BUILD_DIR)/$@

clean:
	rm -rf $(BUILD_DIR)

flash:
	st-flash write $(BUILD_DIR)/$(TARGET).bin 0x8000000

.PHONY: all clean flash
