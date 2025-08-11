# Makefile for krep Advanced DLL Examples
# 
# 编译所有示例程序的 Makefile

CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11
INCLUDES = -I../include
LDFLAGS = -L../lib
LIBS = -lkrep_advanced

# 目标文件
TARGETS = basic_search.exe file_search.exe performance_demo.exe

# 源文件
SOURCES = basic_search.c file_search.c performance_demo.c

.PHONY: all clean help

# 默认目标
all: $(TARGETS)
	@echo "✅ 所有示例程序编译完成!"
	@echo ""
	@echo "可执行文件:"
	@for target in $(TARGETS); do \
		echo "  - $$target"; \
	done
	@echo ""
	@echo "运行示例:"
	@echo "  ./basic_search.exe      - 基础搜索功能演示"
	@echo "  ./file_search.exe       - 文件搜索功能演示"
	@echo "  ./performance_demo.exe  - 性能测试和优化演示"

# 编译规则
%.exe: %.c
	@echo "🔨 编译 $< -> $@"
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LDFLAGS) $(LIBS) -o $@

# 单独编译目标
basic_search.exe: basic_search.c
	@echo "🔨 编译基础搜索示例..."
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LDFLAGS) $(LIBS) -o $@

file_search.exe: file_search.c
	@echo "🔨 编译文件搜索示例..."
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LDFLAGS) $(LIBS) -o $@

performance_demo.exe: performance_demo.c
	@echo "🔨 编译性能演示..."
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LDFLAGS) $(LIBS) -o $@

# 清理编译结果
clean:
	@echo "🗑️  清理编译文件..."
	rm -f $(TARGETS)
	rm -f *.o
	@echo "✅ 清理完成!"

# 帮助信息
help:
	@echo "krep Advanced DLL Examples - 编译帮助"
	@echo "======================================"
	@echo ""
	@echo "可用目标:"
	@echo "  all                - 编译所有示例 (默认)"
	@echo "  basic_search.exe   - 编译基础搜索示例"
	@echo "  file_search.exe    - 编译文件搜索示例"
	@echo "  performance_demo.exe - 编译性能演示"
	@echo "  clean              - 清理编译文件"
	@echo "  help               - 显示此帮助信息"
	@echo ""
	@echo "编译要求:"
	@echo "  - MinGW-w64 或兼容的 GCC 编译器"
	@echo "  - krep_advanced.dll 在 ../lib/ 目录"
	@echo "  - krep_advanced.h 在 ../include/ 目录"
	@echo ""
	@echo "使用示例:"
	@echo "  make                # 编译所有示例"
	@echo "  make clean          # 清理文件"
	@echo "  make basic_search.exe  # 仅编译基础搜索示例"

# 测试目标 (可选)
test: all
	@echo "🧪 运行示例测试..."
	@echo ""
	@echo "1. 基础搜索测试:"
	@./basic_search.exe
	@echo ""
	@echo "2. 文件搜索测试:"
	@./file_search.exe  
	@echo ""
	@echo "3. 性能演示测试:"
	@./performance_demo.exe
	@echo ""
	@echo "✅ 所有测试完成!"