# RK3506 Makefile (Linux 交叉编译)
# 使用 Buildroot 生成的交叉工具链

TARGET  = oled_gateway
BUILD   = build

CXX     = arm-linux-gnueabihf-g++
CXXFLAGS = -std=c++11 -Wall -O2 -g
CXXFLAGS += -Iinc -DMG_ENABLE_HTTP_WEBSOCKET=1

# 需要将 mongoose.c 和 mongoose.h 放在项目目录
LDFLAGS  = -lpthread

SRC = src/main.cpp src/protocol.cpp src/uart_adapter.cpp \
      src/cmd_dispatcher.cpp src/web_server.cpp src/tcp_server.cpp \
      mongoose.c

all: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(BUILD)/$(TARGET) $(LDFLAGS)
	@echo "Build complete: $(BUILD)/$(TARGET)"

clean:
	rm -rf $(BUILD)

# 部署到目标设备 (修改 IP 和目标路径)
deploy: $(TARGET)
	scp $(BUILD)/$(TARGET) root@192.168.1.100:/root/
	scp web/index.html root@192.168.1.100:/root/web/

.PHONY: all clean deploy
