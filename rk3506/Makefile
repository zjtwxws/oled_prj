# RK3506 Makefile (Linux 交叉编译)
# 使用 Buildroot 生成的交叉工具链
# 开发语言: C (C11)

TARGET  = oled_gateway
BUILD   = build

CC      = arm-linux-gnueabihf-gcc
CFLAGS  = -std=c11 -Wall -O2 -g
CFLAGS  += -Iinc -DMG_ENABLE_HTTP_WEBSOCKET=1

# 需要将 mongoose.c 和 mongoose.h 放在项目目录
LDFLAGS  = -lpthread

SRC = src/main.c src/protocol.c src/uart_adapter.c \
      src/cmd_dispatcher.c src/web_server.c src/tcp_server.c \
      mongoose.c

all: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(SRC) -o $(BUILD)/$(TARGET) $(LDFLAGS)
	@echo "Build complete: $(BUILD)/$(TARGET)"

clean:
	rm -rf $(BUILD)

# 部署到目标设备 (修改 IP 和目标路径)
deploy: $(TARGET)
	scp $(BUILD)/$(TARGET) root@192.168.1.100:/root/
	scp web/index.html root@192.168.1.100:/root/web/

.PHONY: all clean deploy
