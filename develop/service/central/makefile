# ============================================================
# 中央服务器 Makefile
# ============================================================

CC = gcc
CFLAGS = -Wall -g -I./include -pthread
LDFLAGS = -lpthread

# 目录
SRC_DIR = src
TEST_DIR = tests
OBJ_DIR = obj

# 源文件
SERVER_SRCS = $(SRC_DIR)/server_main.c \
              $(SRC_DIR)/central_server.c \
              $(SRC_DIR)/protocol_parser.c \
              $(SRC_DIR)/thread_pool.c

TEST_SRCS = $(TEST_DIR)/test_parser.c \
            $(SRC_DIR)/protocol_parser.c

# 目标文件
SERVER_OBJS = $(SERVER_SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
TEST_OBJS = $(TEST_SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
TEST_OBJS := $(TEST_OBJS:$(TEST_DIR)/%.c=$(OBJ_DIR)/%.o)

# 可执行文件
SERVER = server
TEST = test_parser

# ============================================================
# 目标
# ============================================================

.PHONY: all clean test run help

all: $(SERVER) $(TEST)

$(SERVER): $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST): $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(TEST_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

test: $(TEST)
	./$(TEST)

run: $(SERVER)
	./$(SERVER) 8888

run-%: $(SERVER)
	./$(SERVER) $*

clean:
	rm -rf $(OBJ_DIR) $(SERVER) $(TEST)

help:
	@echo "=========================================="
	@echo "  中央服务器 Makefile 帮助"
	@echo "=========================================="
	@echo ""
	@echo "  make          - 编译服务器和测试"
	@echo "  make test     - 编译并运行测试"
	@echo "  make run      - 编译并运行服务器 (端口8888)"
	@echo "  make run-9999 - 编译并运行服务器 (端口9999)"
	@echo "  make clean    - 清理编译文件"
	@echo "  make help     - 显示帮助"
	@echo ""