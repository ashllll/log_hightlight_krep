# krep Windows DLL 完整指南

## 概述

已成功将 krep 高性能字符串搜索工具编译为 Windows DLL，提供以下功能：

### 已生成的文件
- `krep_simple.dll` - Windows 动态链接库
- `libkrep_simple.dll.a` - 链接导入库  
- `krep_simple.h` - C/C++ 头文件
- `example_usage.c` - 使用示例

## 核心功能实现

### 1. 基础搜索功能 ✅
- 字符串模式搜索（Boyer-Moore-Horspool 算法）
- 区分大小写/不区分大小写搜索（`-i` 选项）
- 整词匹配（`-w` 选项）
- 匹配次数限制（`-m` 选项）

### 2. API 函数

#### 匹配结果管理
```c
// 初始化匹配结果
match_result_t* match_result_init(uint64_t initial_capacity);

// 添加匹配结果
bool match_result_add(match_result_t *result, size_t start_offset, size_t end_offset);

// 释放匹配结果
void match_result_free(match_result_t *result);
```

#### 核心搜索函数
```c
// 高级搜索（支持所有参数）
uint64_t search_string_simple(const search_params_simple_t *params, 
                             const char *text, size_t text_len, 
                             match_result_t *result);

// 简单搜索
uint64_t search_buffer(const char *pattern, size_t pattern_len,
                      const char *text, size_t text_len,
                      bool case_sensitive, bool whole_word,
                      match_result_t *result);
```

## 原版 krep 命令行选项的实现方案

### ✅ 已实现选项

| 选项 | 功能 | DLL 实现方式 |
|------|------|--------------|
| `-i, --ignore-case` | 不区分大小写搜索 | `case_sensitive = false` |
| `-c, --count` | 仅统计匹配行数 | 传入 `result = NULL` |
| `-w, --word-regexp` | 仅匹配整词 | `whole_word = true` |
| `-m NUM, --max-count=NUM` | 限制匹配数量 | `params.max_count = NUM` |

### 📝 需要额外实现的选项

#### 文件操作相关
```c
// 扩展的文件搜索函数（需要实现）
typedef struct {
    char **filenames;
    size_t num_files;
    bool recursive;
    int thread_count;
} file_search_params_t;

// 搜索文件
int search_file_dll(const search_params_simple_t *search_params,
                   const char *filename,
                   void (*output_callback)(const char *line, size_t line_num, const match_result_t *matches));

// 递归搜索目录
int search_directory_dll(const search_params_simple_t *search_params,
                        const char *directory,
                        bool recursive,
                        void (*output_callback)(const char *filename, const char *line, size_t line_num));
```

#### 多模式搜索 (`-e` 选项)
```c
typedef struct {
    const char **patterns;
    size_t *pattern_lens;
    size_t num_patterns;
    bool case_sensitive;
    bool whole_word;
    size_t max_count;
} multi_pattern_params_t;

// 多模式搜索
uint64_t search_multiple_patterns(const multi_pattern_params_t *params,
                                 const char *text, size_t text_len,
                                 match_result_t *result);
```

#### 正则表达式支持 (`-E` 选项)
```c
// 需要集成 PCRE 或类似库
typedef struct {
    const char *regex_pattern;
    bool case_sensitive;
    size_t max_count;
} regex_params_t;

uint64_t search_regex(const regex_params_t *params,
                     const char *text, size_t text_len,
                     match_result_t *result);
```

## 使用示例

### 基本使用
```c
#include "krep_simple.h"

int main() {
    // 搜索文本
    const char *text = "Hello World, this is a test";
    const char *pattern = "test";
    
    // 创建结果容器
    match_result_t *result = match_result_init(10);
    
    // 执行搜索
    uint64_t matches = search_buffer(pattern, strlen(pattern), 
                                   text, strlen(text), 
                                   true, false, result);
    
    printf("找到 %llu 个匹配\n", matches);
    
    // 输出匹配位置
    for (uint64_t i = 0; i < result->count; i++) {
        printf("匹配 %llu: 位置 %zu-%zu\n", 
               i+1, 
               result->positions[i].start_offset,
               result->positions[i].end_offset-1);
    }
    
    // 清理
    match_result_free(result);
    return 0;
}
```

### 高级搜索参数
```c
search_params_simple_t params = {
    .pattern = "test",
    .pattern_len = 4,
    .case_sensitive = false,    // -i 选项
    .whole_word = true,         // -w 选项  
    .max_count = 5              // -m 选项
};

uint64_t matches = search_string_simple(&params, text, text_len, result);
```

## 性能特点

### ✅ 已优化功能
- Boyer-Moore-Horspool 快速字符串匹配算法
- 内存高效的匹配结果存储
- 动态数组管理，避免内存浪费

### 🔧 可扩展优化
- SIMD 指令集优化（SSE4.2, AVX2）
- 多线程并行搜索
- 内存映射文件 I/O

## 编译和部署

### 构建 DLL
```bash
# 安装 MinGW-w64 交叉编译器
brew install mingw-w64  # macOS
# 或
sudo apt install gcc-mingw-w64  # Linux

# 编译 DLL
make -f Makefile.simple dll
```

### 在 Windows 项目中使用

#### 1. Visual Studio
1. 将 `krep_simple.dll` 复制到输出目录
2. 将 `libkrep_simple.dll.a` 添加到链接库
3. 包含 `krep_simple.h` 头文件

#### 2. GCC/MinGW
```bash
gcc your_program.c -L. -lkrep_simple -o your_program.exe
```

#### 3. CMake
```cmake
find_library(KREP_SIMPLE_LIB krep_simple)
target_link_libraries(your_target ${KREP_SIMPLE_LIB})
```

## 扩展开发建议

### 1. 完整功能实现
要实现原版 krep 的所有功能，建议按优先级实现：

1. **高优先级**
   - 文件搜索功能 (`search_file`)
   - 多模式搜索 (`-e` 选项)
   - 仅输出匹配部分 (`-o` 选项)

2. **中优先级**  
   - 正则表达式支持 (`-E` 选项)
   - 递归目录搜索 (`-r` 选项)
   - 从文件读取模式 (`-f` 选项)

3. **低优先级**
   - 多线程支持 (`-t` 选项)
   - SIMD 优化 (`--no-simd` 选项)
   - 颜色输出 (`--color` 选项)

### 2. 架构建议
```c
// 统一的搜索接口
typedef struct {
    // 搜索选项
    bool ignore_case;           // -i
    bool count_only;           // -c  
    bool only_matching;        // -o
    bool extended_regexp;      // -E
    bool fixed_strings;        // -F
    bool recursive;            // -r
    bool whole_word;           // -w
    
    // 参数
    char **patterns;           // -e
    size_t num_patterns;
    char *pattern_file;        // -f
    size_t max_count;          // -m
    int thread_count;          // -t
    char *search_string;       // -s
    
    // 输出控制
    enum { COLOR_AUTO, COLOR_ALWAYS, COLOR_NEVER } color_mode;
    bool no_simd;
} krep_options_t;

// 统一搜索函数
int krep_search(const krep_options_t *options, 
               const char **targets, 
               size_t num_targets,
               void (*output_callback)(const char *filename, 
                                     size_t line_num, 
                                     const char *line,
                                     const match_result_t *matches));
```

## 许可证

与原始 krep 项目相同 (BSD-2 License)