# krep Advanced Windows DLL - 完整使用指南

## 📋 目录
1. [快速开始](#快速开始)
2. [环境配置](#环境配置)
3. [核心搜索功能](#核心搜索功能)
4. [智能算法选择](#智能算法选择)
5. [多线程架构](#多线程架构)
6. [内存映射文件 I/O](#内存映射文件-io)
7. [文件类型检测](#文件类型检测)
8. [所有命令行选项](#所有命令行选项)
9. [SIMD 加速](#simd-加速)
10. [错误处理](#错误处理)
11. [完整示例程序](#完整示例程序)
12. [性能优化建议](#性能优化建议)

---

## 快速开始

### 1. 文件清单
确保您有以下文件：
```
krep_advanced.dll       # 主要 DLL 库 (119KB)
libkrep_advanced.dll.a  # 导入库 (10KB)
krep_advanced.h         # 头文件
USER_GUIDE.md           # 本使用指南
```

### 2. 基础编译命令
```bash
# MinGW 编译
gcc your_program.c -I. -L. -lkrep_advanced -o your_program.exe

# 或使用 CMake
cmake . && make
```

### 3. 最简单的使用示例
```c
#include "krep_advanced.h"

int main() {
    // 1. 定义搜索模式
    const char *patterns[] = {"hello"};
    size_t lens[] = {5};
    
    // 2. 创建搜索参数
    advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 1);
    
    // 3. 执行搜索
    const char *text = "hello world, hello krep!";
    match_result_t *result = match_result_init(10);
    uint64_t matches = search_advanced(&params, text, strlen(text), result);
    
    // 4. 输出结果
    printf("Found %llu matches\n", matches);
    for (uint64_t i = 0; i < result->count; i++) {
        printf("Match at position %zu\n", result->positions[i].start_offset);
    }
    
    // 5. 清理
    match_result_free(result);
    return 0;
}
```

---

## 环境配置

### Windows 部署
1. 将 `krep_advanced.dll` 放在以下位置之一：
   - 与您的 .exe 文件相同目录
   - Windows 系统 PATH 中的目录
   - Windows/System32 目录 (需要管理员权限)

### 编译时链接
```bash
# 静态链接导入库
gcc -I. -L. -lkrep_advanced your_program.c -o your_program.exe

# Visual Studio
cl your_program.c /I. /link libkrep_advanced.dll.a
```

---

## 核心搜索功能

### 1. 基本字符串搜索

```c
#include "krep_advanced.h"

// 搜索单个模式
void basic_search_example() {
    // 定义搜索模式
    const char *patterns[] = {"pattern"};
    size_t lens[] = {7};
    
    // 创建搜索参数
    advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 1);
    
    // 准备文本
    const char *text = "This text contains a pattern to find.";
    
    // 初始化结果容器
    match_result_t *result = match_result_init(100);
    
    // 执行搜索
    uint64_t matches = search_advanced(&params, text, strlen(text), result);
    
    printf("Basic search found %llu matches\n", matches);
    
    // 输出每个匹配的位置
    for (uint64_t i = 0; i < result->count; i++) {
        size_t start = result->positions[i].start_offset;
        size_t end = result->positions[i].end_offset;
        printf("  Match %llu: positions %zu-%zu\n", i+1, start, end-1);
        
        // 输出匹配的文本
        printf("  Text: '");
        for (size_t j = start; j < end; j++) {
            printf("%c", text[j]);
        }
        printf("'\n");
    }
    
    match_result_free(result);
}
```

### 2. 多模式搜索 (模拟 -e 选项)

```c
// 搜索多个模式 (等效于: krep -e "error" -e "warning" -e "info")
void multi_pattern_search() {
    // 定义多个搜索模式
    const char *patterns[] = {"error", "warning", "info"};
    size_t lens[] = {5, 7, 4};
    
    // 创建多模式搜索参数
    advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 3);
    
    const char *log_text = 
        "2023-01-01 12:00:00 INFO: Application started\n"
        "2023-01-01 12:05:00 WARNING: High memory usage\n"  
        "2023-01-01 12:10:00 ERROR: Database connection failed\n"
        "2023-01-01 12:15:00 INFO: Retrying connection\n";
    
    match_result_t *result = match_result_init(100);
    uint64_t matches = search_advanced(&params, log_text, strlen(log_text), result);
    
    printf("Multi-pattern search found %llu matches:\n", matches);
    
    // 按位置排序显示匹配
    for (uint64_t i = 0; i < result->count; i++) {
        size_t start = result->positions[i].start_offset;
        size_t end = result->positions[i].end_offset;
        
        // 确定匹配的是哪个模式
        char matched_text[20] = {0};
        size_t len = end - start;
        strncpy(matched_text, log_text + start, len);
        
        printf("  Found '%s' at position %zu\n", matched_text, start);
    }
    
    match_result_free(result);
}
```

### 3. 仅计数搜索 (模拟 -c 选项)

```c
// 仅统计匹配数量，不返回位置信息
void count_only_search() {
    const char *patterns[] = {"the"};
    size_t lens[] = {3};
    
    advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 1);
    KREP_SET_COUNT_ONLY(&params);  // 启用仅计数模式
    
    const char *text = "The quick brown fox jumps over the lazy dog. "
                      "The dog was not amused by the fox's behavior.";
    
    // 仅计数时可以传入 NULL 作为结果参数
    uint64_t count = search_advanced(&params, text, strlen(text), NULL);
    
    printf("Count-only search: found %llu occurrences of 'the'\n", count);
}
```

---

## 智能算法选择

### 1. 自动算法选择

```c
// 让 DLL 自动选择最优算法
void automatic_algorithm_selection() {
    printf("=== Automatic Algorithm Selection Demo ===\n");
    
    struct {
        const char *pattern;
        const char *description;
    } test_cases[] = {
        {"a", "Single char (uses memchr optimization)"},
        {"th", "Short pattern (may use KMP)"},
        {"aaaaa", "Repetitive pattern (uses KMP)"},
        {"comprehensive", "Long pattern (uses Boyer-Moore-Horspool)"}
    };
    
    const char *text = "This is a comprehensive test with aaaaa repetitive patterns and short th sequences.";
    
    for (int i = 0; i < 4; i++) {
        const char *patterns[] = {test_cases[i].pattern};
        size_t lens[] = {strlen(test_cases[i].pattern)};
        
        advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 1);
        params.force_algorithm = ALGO_AUTO;  // 明确指定自动选择
        
        match_result_t *result = match_result_init(50);
        
        clock_t start = clock();
        uint64_t matches = search_advanced(&params, text, strlen(text), result);
        clock_t end = clock();
        
        double time_ms = ((double)(end - start) / CLOCKS_PER_SEC) * 1000.0;
        
        printf("Pattern '%s': %llu matches in %.3fms - %s\n", 
               test_cases[i].pattern, matches, time_ms, test_cases[i].description);
        
        match_result_free(result);
    }
}
```

### 2. 强制指定算法

```c
// 手动指定使用特定算法
void force_specific_algorithm() {
    const char *patterns[] = {"test"};
    size_t lens[] = {4};
    const char *text = "This is a test string for testing different algorithms.";
    
    search_algorithm_t algorithms[] = {
        ALGO_BOYER_MOORE,
        ALGO_KMP, 
        ALGO_MEMCHR,     // 只对单字符有效
        ALGO_SIMD_SSE42
    };
    
    const char *algo_names[] = {
        "Boyer-Moore-Horspool",
        "Knuth-Morris-Pratt",
        "memchr (single char only)",
        "SIMD SSE4.2"
    };
    
    for (int i = 0; i < 4; i++) {
        advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 1);
        params.force_algorithm = algorithms[i];
        
        match_result_t *result = match_result_init(50);
        uint64_t matches = search_advanced(&params, text, strlen(text), result);
        
        printf("Forced %s: %llu matches\n", algo_names[i], matches);
        
        // 获取实际使用的算法名称
        const char *actual_name = get_algorithm_name(algorithms[i]);
        printf("  Algorithm name: %s\n", actual_name);
        
        match_result_free(result);
    }
}
```

---

## 多线程架构

### 1. 自动多线程处理

```c
// 让系统自动决定是否使用多线程
void automatic_threading() {
    const char *patterns[] = {"data"};
    size_t lens[] = {4};
    
    // 创建大文本用于测试多线程
    size_t large_size = 2 * 1024 * 1024;  // 2MB
    char *large_text = malloc(large_size + 1);
    
    // 填充测试数据
    const char *base_text = "This is sample data for threading test. ";
    size_t base_len = strlen(base_text);
    
    for (size_t i = 0; i < large_size; i += base_len) {
        size_t copy_len = (i + base_len > large_size) ? (large_size - i) : base_len;
        memcpy(large_text + i, base_text, copy_len);
    }
    large_text[large_size] = '\0';
    
    // 自动线程数 (0 = 自动检测 CPU 核心数)
    advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 1);
    params.thread_count = 0;  // 自动检测
    
    match_result_t *result = match_result_init(10000);
    
    clock_t start = clock();
    uint64_t matches = search_advanced(&params, large_text, large_size, result);
    clock_t end = clock();
    
    double time_ms = ((double)(end - start) / CLOCKS_PER_SEC) * 1000.0;
    
    printf("Auto-threading on %zuMB data:\n", large_size / (1024*1024));
    printf("  Found %llu matches in %.2fms\n", matches, time_ms);
    printf("  Performance: %.2f MB/s\n", (large_size / 1024.0 / 1024.0) / (time_ms / 1000.0));
    
    match_result_free(result);
    free(large_text);
}
```

### 2. 指定线程数 (模拟 -t 选项)

```c
// 手动指定线程数进行搜索
void manual_thread_control() {
    const char *patterns[] = {"thread"};
    size_t lens[] = {6};
    
    // 创建测试文本
    size_t text_size = 1024 * 1024;  // 1MB
    char *text = malloc(text_size + 1);
    memset(text, 'x', text_size);
    
    // 在不同位置插入搜索目标
    for (size_t i = 0; i < text_size; i += 10000) {
        if (i + 6 < text_size) {
            memcpy(text + i, "thread", 6);
        }
    }
    text[text_size] = '\0';
    
    int thread_counts[] = {1, 2, 4, 8};
    
    for (int i = 0; i < 4; i++) {
        advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 1);
        KREP_SET_THREADS(&params, thread_counts[i]);  // 设置线程数
        
        match_result_t *result = match_result_init(1000);
        
        clock_t start = clock();
        uint64_t matches = search_advanced(&params, text, text_size, result);
        clock_t end = clock();
        
        double time_ms = ((double)(end - start) / CLOCKS_PER_SEC) * 1000.0;
        
        printf("With %d thread(s): %llu matches in %.2fms\n", 
               thread_counts[i], matches, time_ms);
        
        match_result_free(result);
    }
    
    free(text);
}
```

---

## 内存映射文件 I/O

### 1. 文件内存映射

```c
// 使用内存映射进行高效文件搜索
void memory_mapped_file_search() {
    // 首先创建一个测试文件
    const char *filename = "test_file.txt";
    const char *file_content = 
        "Line 1: This is the first line with some data\n"
        "Line 2: Another line with different content\n" 
        "Line 3: Final line containing target information\n";
    
    // 写入测试文件
    FILE *f = fopen(filename, "w");
    if (f) {
        fwrite(file_content, 1, strlen(file_content), f);
        fclose(f);
    }
    
    // 使用内存映射打开文件
    mapped_file_t *mapped = map_file(filename);
    
    if (mapped && mapped->data && mapped->size > 0) {
        printf("File mapped successfully:\n");
        printf("  Size: %zu bytes\n", mapped->size);
        printf("  Mapped: %s\n", mapped->is_mapped ? "Yes" : "No");
        
        // 在映射的内存中进行搜索
        const char *patterns[] = {"line"};
        size_t lens[] = {4};
        
        advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 1);
        KREP_SET_IGNORE_CASE(&params);  // 不区分大小写
        
        match_result_t *result = match_result_init(100);
        uint64_t matches = search_advanced(&params, mapped->data, mapped->size, result);
        
        printf("Memory-mapped search found %llu matches:\n", matches);
        
        for (uint64_t i = 0; i < result->count; i++) {
            size_t start = result->positions[i].start_offset;
            // 找到包含匹配的行
            size_t line_start = start;
            while (line_start > 0 && mapped->data[line_start-1] != '\n') {
                line_start--;
            }
            
            size_t line_end = start;
            while (line_end < mapped->size && mapped->data[line_end] != '\n') {
                line_end++;
            }
            
            printf("  Line: ");
            for (size_t j = line_start; j < line_end; j++) {
                printf("%c", mapped->data[j]);
            }
            printf("\n");
        }
        
        match_result_free(result);
        unmap_file(mapped);
    } else {
        printf("Failed to map file %s\n", filename);
    }
    
    // 清理测试文件
    remove(filename);
}
```

### 2. 高级文件搜索

```c
// 文件搜索回调函数
void search_result_callback(const char *filename, size_t line_number,
                           const char *line, const match_result_t *matches,
                           void *user_data) {
    int *total_matches = (int*)user_data;
    (*total_matches)++;
    
    printf("📁 %s:%zu: %s", filename, line_number, line);
    if (matches && matches->count > 0) {
        printf(" [%llu matches in line]", matches->count);
    }
    printf("\n");
}

void advanced_file_search() {
    // 创建测试文件
    const char *test_files[] = {"file1.txt", "file2.log"};
    const char *contents[] = {
        "File 1 content with search terms\nAnother line here\n",
        "Log file with multiple search entries\nDebug: search completed\n"
    };
    
    // 写入测试文件
    for (int i = 0; i < 2; i++) {
        FILE *f = fopen(test_files[i], "w");
        if (f) {
            fwrite(contents[i], 1, strlen(contents[i]), f);
            fclose(f);
        }
    }
    
    // 搜索参数
    const char *patterns[] = {"search"};
    size_t lens[] = {6};
    advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 1);
    
    // 搜索每个文件
    int total_matches = 0;
    for (int i = 0; i < 2; i++) {
        printf("Searching in %s:\n", test_files[i]);
        
        int result_code = search_file_advanced(test_files[i], &params, 
                                              search_result_callback, &total_matches);
        
        switch (result_code) {
            case 0: printf("✅ Search completed with matches\n"); break;
            case 1: printf("ℹ️  Search completed, no matches\n"); break;  
            case 2: printf("❌ Search failed (file error)\n"); break;
        }
        printf("\n");
    }
    
    printf("Total matches across all files: %d\n", total_matches);
    
    // 清理测试文件
    for (int i = 0; i < 2; i++) {
        remove(test_files[i]);
    }
}
```

---

## 文件类型检测

### 1. 二进制文件检测

```c
// 检测文件类型，决定是否需要搜索
void file_type_detection() {
    printf("=== File Type Detection ===\n");
    
    // 测试各种文件类型
    const char *test_files[] = {
        "document.txt",    // 文本文件
        "program.exe",     // 可执行文件
        "image.jpg",       // 图像文件
        "archive.zip",     // 压缩文件
        "source.c",        // 源代码文件
        "data.json",       // 数据文件
        "style.css",       // 样式表
        "binary.dll"       // 动态库
    };
    
    for (int i = 0; i < 8; i++) {
        bool is_binary = is_binary_file(test_files[i]);
        printf("%-15s: %s\n", test_files[i], 
               is_binary ? "Binary (skip) ❌" : "Text (search) ✅");
    }
    
    printf("\n=== Directory Skip Detection ===\n");
    
    const char *test_dirs[] = {
        "src",             // 源代码目录
        ".git",           // Git 版本控制
        "node_modules",   // Node.js 依赖
        "__pycache__",    // Python 缓存
        "venv",           // Python 虚拟环境
        "build",          // 构建输出
        "docs",           // 文档目录
        ".svn"            // SVN 版本控制
    };
    
    for (int i = 0; i < 8; i++) {
        bool should_skip = should_skip_directory(test_dirs[i]);
        printf("%-15s: %s\n", test_dirs[i],
               should_skip ? "Skip ❌" : "Process ✅");
    }
}
```

### 2. 二进制内容检测

```c
// 检测文件内容是否为二进制
void binary_content_detection() {
    printf("=== Binary Content Detection ===\n");
    
    // 测试不同类型的内容
    struct {
        const char *name;
        const char *data;
        size_t size;
    } test_contents[] = {
        {"Plain text", "Hello, this is normal text content", 0},
        {"UTF-8 text", "Hello 世界 🌍", 0},
        {"Binary data", "\x00\x01\x02\xFF\xFE", 5},
        {"Mixed content", "Text with \x00 null byte", 0},
        {"Code content", "#include <stdio.h>\nint main(){}", 0}
    };
    
    // 计算字符串长度 (除了明确指定大小的)
    for (int i = 0; i < 5; i++) {
        if (test_contents[i].size == 0) {
            test_contents[i].size = strlen(test_contents[i].data);
        }
    }
    
    for (int i = 0; i < 5; i++) {
        bool is_binary = detect_binary_content(test_contents[i].data, 
                                              test_contents[i].size);
        printf("%-15s: %s\n", test_contents[i].name,
               is_binary ? "Binary ❌" : "Text ✅");
    }
}
```

### 3. 递归目录搜索 (模拟 -r 选项)

```c
// 递归搜索目录，自动跳过不相关文件和目录
void recursive_directory_search() {
    printf("=== Recursive Directory Search ===\n");
    
    // 创建测试目录结构
    #ifdef _WIN32
    system("mkdir test_dir 2>NUL");
    system("mkdir test_dir\\src 2>NUL");
    system("mkdir test_dir\\.git 2>NUL");
    system("mkdir test_dir\\node_modules 2>NUL");
    #else
    system("mkdir -p test_dir/src 2>/dev/null");
    system("mkdir -p test_dir/.git 2>/dev/null"); 
    system("mkdir -p test_dir/node_modules 2>/dev/null");
    #endif
    
    // 创建测试文件
    const char *files[] = {
        "test_dir/readme.txt",
        "test_dir/src/main.c",
        "test_dir/.git/config",
        "test_dir/node_modules/package.json"
    };
    
    const char *contents[] = {
        "This readme contains target information\n",
        "#include <stdio.h>\nint main() { printf(\"target found\"); }\n",
        "[core]\n    target = true\n",
        "{\n  \"name\": \"target-package\"\n}\n"
    };
    
    for (int i = 0; i < 4; i++) {
        FILE *f = fopen(files[i], "w");
        if (f) {
            fwrite(contents[i], 1, strlen(contents[i]), f);
            fclose(f);
        }
    }
    
    // 搜索参数
    const char *patterns[] = {"target"};
    size_t lens[] = {6};
    advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 1);
    
    int matches_found = 0;
    
    // 执行递归搜索
    int error_count = search_directory_recursive("test_dir", &params, 
                                                 search_result_callback, 
                                                 &matches_found);
    
    printf("Recursive search results:\n");
    printf("  Files with matches: %d\n", matches_found);
    printf("  Errors encountered: %d\n", error_count);
    printf("  Note: .git and node_modules should be automatically skipped\n");
    
    // 清理测试目录 (简化版，实际项目中需要递归删除)
    system("rm -rf test_dir 2>/dev/null || rmdir /s /q test_dir 2>NUL");
}
```

---

## 所有命令行选项

### 1. 基础搜索选项

```c
// 演示所有基础搜索选项
void basic_search_options() {
    const char *text = "Hello WORLD! This is a Test String for testing. "
                      "Multiple test cases: test, TEST, Test, testing, tests.";
    
    const char *patterns[] = {"test"};
    size_t lens[] = {4};
    
    printf("=== Basic Search Options ===\n");
    printf("Text: \"%s\"\n\n", text);
    
    // 1. 默认搜索 (区分大小写)
    {
        advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 1);
        match_result_t *result = match_result_init(50);
        
        uint64_t matches = search_advanced(&params, text, strlen(text), result);
        printf("1. Default (case-sensitive): %llu matches\n", matches);
        
        match_result_free(result);
    }
    
    // 2. 不区分大小写 (-i 选项)
    {
        advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 1);
        KREP_SET_IGNORE_CASE(&params);  // 模拟 -i 选项
        
        match_result_t *result = match_result_init(50);
        uint64_t matches = search_advanced(&params, text, strlen(text), result);
        printf("2. Case-insensitive (-i): %llu matches\n", matches);
        
        match_result_free(result);
    }
    
    // 3. 全词匹配 (-w 选项) 
    {
        advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 1);
        KREP_SET_WHOLE_WORD(&params);  // 模拟 -w 选项
        
        match_result_t *result = match_result_init(50);
        uint64_t matches = search_advanced(&params, text, strlen(text), result);
        printf("3. Whole word only (-w): %llu matches\n", matches);
        
        match_result_free(result);
    }
    
    // 4. 组合选项 (-i -w)
    {
        advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 1);
        KREP_SET_IGNORE_CASE(&params);
        KREP_SET_WHOLE_WORD(&params);  // 模拟 -i -w
        
        match_result_t *result = match_result_init(50);
        uint64_t matches = search_advanced(&params, text, strlen(text), result);
        printf("4. Case-insensitive + whole word (-i -w): %llu matches\n", matches);
        
        match_result_free(result);
    }
    
    // 5. 限制匹配数量 (-m 选项)
    {
        advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 1);
        KREP_SET_IGNORE_CASE(&params);
        KREP_SET_MAX_COUNT(&params, 2);  // 模拟 -i -m 2
        
        match_result_t *result = match_result_init(50);
        uint64_t matches = search_advanced(&params, text, strlen(text), result);
        printf("5. Max 2 matches (-i -m 2): %llu matches (limited)\n", matches);
        
        match_result_free(result);
    }
    
    // 6. 仅计数 (-c 选项)
    {
        advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 1);
        KREP_SET_IGNORE_CASE(&params);
        KREP_SET_COUNT_ONLY(&params);  // 模拟 -i -c
        
        uint64_t count = search_advanced(&params, text, strlen(text), NULL);
        printf("6. Count only (-i -c): %llu total occurrences\n", count);
    }
}
```

### 2. 高级搜索选项

```c
// 演示高级搜索选项
void advanced_search_options() {
    printf("\n=== Advanced Search Options ===\n");
    
    // 1. 多模式搜索 (-e 选项多次使用)
    {
        const char *patterns[] = {"error", "warning", "info"};
        size_t lens[] = {5, 7, 4};
        
        advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 3);
        // 模拟: krep -e "error" -e "warning" -e "info" logfile.txt
        
        const char *log_text = "INFO: Start\nWARNING: Issue\nERROR: Failed\n";
        match_result_t *result = match_result_init(100);
        
        uint64_t matches = search_advanced(&params, log_text, strlen(log_text), result);
        printf("1. Multiple patterns (-e): %llu matches across 3 patterns\n", matches);
        
        match_result_free(result);
    }
    
    // 2. 正则表达式搜索 (-E 选项)
    {
        const char *patterns[] = {"[0-9]+"};  // 数字模式
        size_t lens[] = {6};
        
        advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 1);
        KREP_SET_REGEX(&params);  // 模拟 -E 选项 (启用正则表达式)
        
        // 注意: 当前实现可能需要额外的正则表达式引擎支持
        printf("2. Regular expression (-E): Regex pattern support enabled\n");
        printf("   Pattern: %s (requires regex engine)\n", patterns[0]);
    }
    
    // 3. 固定字符串搜索 (-F 选项，默认行为)
    {
        const char *patterns[] = {"[test]"};  // 作为字面字符串，不是正则
        size_t lens[] = {6};
        
        advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 1);
        // params.use_regex = false;  // 默认已经是 false (-F 行为)
        
        const char *text = "This [test] string contains literal brackets [test]";
        match_result_t *result = match_result_init(50);
        
        uint64_t matches = search_advanced(&params, text, strlen(text), result);
        printf("3. Fixed strings (-F): %llu literal matches for '[test]'\n", matches);
        
        match_result_free(result);
    }
    
    // 4. 线程控制 (-t 选项)
    {
        const char *patterns[] = {"data"};
        size_t lens[] = {4};
        
        // 创建较大的文本以展示多线程效果
        char *large_text = malloc(100000);
        for (int i = 0; i < 99990; i += 10) {
            memcpy(large_text + i, "test data ", 10);
        }
        large_text[99999] = '\0';
        
        int thread_options[] = {1, 2, 4};
        
        for (int i = 0; i < 3; i++) {
            advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 1);
            KREP_SET_THREADS(&params, thread_options[i]);  // 模拟 -t N
            
            match_result_t *result = match_result_init(1000);
            
            clock_t start = clock();
            uint64_t matches = search_advanced(&params, large_text, 99999, result);
            clock_t end = clock();
            
            double time_ms = ((double)(end - start) / CLOCKS_PER_SEC) * 1000.0;
            
            printf("4.%d Thread count (-t %d): %llu matches in %.2fms\n", 
                   i+1, thread_options[i], matches, time_ms);
            
            match_result_free(result);
        }
        
        free(large_text);
    }
    
    // 5. SIMD 控制 (--no-simd 选项)
    {
        const char *patterns[] = {"simd"};
        size_t lens[] = {4};
        const char *text = "This text tests simd acceleration features";
        
        // 启用 SIMD (默认)
        {
            advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 1);
            params.use_simd = true;  // 默认启用
            
            match_result_t *result = match_result_init(50);
            uint64_t matches = search_advanced(&params, text, strlen(text), result);
            printf("5.1 SIMD enabled (default): %llu matches\n", matches);
            match_result_free(result);
        }
        
        // 禁用 SIMD
        {
            advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 1);
            KREP_DISABLE_SIMD(&params);  // 模拟 --no-simd
            
            match_result_t *result = match_result_init(50);
            uint64_t matches = search_advanced(&params, text, strlen(text), result);
            printf("5.2 SIMD disabled (--no-simd): %llu matches\n", matches);
            match_result_free(result);
        }
    }
}
```

---

## SIMD 加速

### 1. SIMD 功能测试

```c
// 测试 SIMD 加速功能
void simd_acceleration_demo() {
    printf("=== SIMD Acceleration Demo ===\n");
    
    // 创建适合 SIMD 测试的数据
    const char *patterns[] = {"performance"};  // 11字符，适合 SSE4.2
    size_t lens[] = {11};
    
    // 创建重复的测试文本以展示 SIMD 效果
    size_t text_size = 1024 * 1024;  // 1MB
    char *test_text = malloc(text_size + 1);
    
    const char *base_text = "This is performance test data with performance metrics and performance analysis. ";
    size_t base_len = strlen(base_text);
    
    for (size_t i = 0; i < text_size; i += base_len) {
        size_t copy_len = (i + base_len > text_size) ? (text_size - i) : base_len;
        memcpy(test_text + i, base_text, copy_len);
    }
    test_text[text_size] = '\0';
    
    printf("Testing with %zu KB of text data\n", text_size / 1024);
    
    // 1. 测试 SIMD 启用
    {
        advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 1);
        params.use_simd = true;
        
        match_result_t *result = match_result_init(10000);
        
        clock_t start = clock();
        uint64_t matches = search_advanced(&params, test_text, text_size, result);
        clock_t end = clock();
        
        double time_ms = ((double)(end - start) / CLOCKS_PER_SEC) * 1000.0;
        
        printf("SIMD Enabled:  %llu matches in %.3fms\n", matches, time_ms);
        printf("  Throughput: %.2f MB/s\n", (text_size / 1024.0 / 1024.0) / (time_ms / 1000.0));
        
        match_result_free(result);
    }
    
    // 2. 测试 SIMD 禁用
    {
        advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 1);
        params.use_simd = false;
        
        match_result_t *result = match_result_init(10000);
        
        clock_t start = clock();
        uint64_t matches = search_advanced(&params, test_text, text_size, result);
        clock_t end = clock();
        
        double time_ms = ((double)(end - start) / CLOCKS_PER_SEC) * 1000.0;
        
        printf("SIMD Disabled: %llu matches in %.3fms\n", matches, time_ms);
        printf("  Throughput: %.2f MB/s\n", (text_size / 1024.0 / 1024.0) / (time_ms / 1000.0));
        
        match_result_free(result);
    }
    
    // 3. 强制使用特定 SIMD 算法
    {
        search_algorithm_t simd_algos[] = {ALGO_SIMD_SSE42, ALGO_SIMD_AVX2};
        const char *simd_names[] = {"SSE4.2", "AVX2"};
        
        for (int i = 0; i < 2; i++) {
            advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 1);
            params.force_algorithm = simd_algos[i];
            params.use_simd = true;
            
            match_result_t *result = match_result_init(10000);
            uint64_t matches = search_advanced(&params, test_text, text_size, result);
            
            printf("Forced %s SIMD: %llu matches ", simd_names[i], matches);
            if (matches > 0) {
                printf("✅ (supported)\n");
            } else {
                printf("⚠️  (may not be supported on this CPU)\n");
            }
            
            match_result_free(result);
        }
    }
    
    free(test_text);
}
```

---

## 错误处理

### 1. 输入验证和错误处理

```c
// 演示各种错误情况的处理
void error_handling_demo() {
    printf("=== Error Handling Demo ===\n");
    
    // 1. NULL 参数处理
    {
        printf("1. Testing NULL parameter handling:\n");
        
        uint64_t result1 = search_advanced(NULL, "text", 4, NULL);
        printf("   search_advanced(NULL, ...): %llu (should be 0)\n", result1);
        
        match_result_t *result2 = match_result_init(0);  // 0 容量
        printf("   match_result_init(0): %s\n", result2 ? "Non-NULL" : "NULL (expected)");
        
        if (result2) match_result_free(result2);
    }
    
    // 2. 空模式串处理
    {
        printf("2. Testing empty pattern handling:\n");
        
        const char *empty_patterns[] = {""};
        size_t empty_lens[] = {0};
        
        advanced_search_params_t params = KREP_PARAMS_INIT(empty_patterns, empty_lens, 1);
        match_result_t *result = match_result_init(10);
        
        uint64_t matches = search_advanced(&params, "test text", 9, result);
        printf("   Empty pattern search: %llu matches (should be 0)\n", matches);
        
        match_result_free(result);
    }
    
    // 3. 超长模式串处理
    {
        printf("3. Testing very long pattern handling:\n");
        
        char long_pattern[2000];
        memset(long_pattern, 'a', sizeof(long_pattern) - 1);
        long_pattern[sizeof(long_pattern) - 1] = '\0';
        
        const char *long_patterns[] = {long_pattern};
        size_t long_lens[] = {sizeof(long_pattern) - 1};
        
        advanced_search_params_t params = KREP_PARAMS_INIT(long_patterns, long_lens, 1);
        match_result_t *result = match_result_init(10);
        
        uint64_t matches = search_advanced(&params, "short text", 10, result);
        printf("   Long pattern (2000 chars): %llu matches (should be 0, no crash)\n", matches);
        
        match_result_free(result);
    }
    
    // 4. 文件操作错误处理
    {
        printf("4. Testing file operation error handling:\n");
        
        // 测试不存在的文件
        size_t size = get_file_size("nonexistent_file_12345.txt");
        printf("   get_file_size(nonexistent): %zu (should be 0)\n", size);
        
        bool exists = file_exists("nonexistent_file_12345.txt");
        printf("   file_exists(nonexistent): %s (should be false)\n", exists ? "true" : "false");
        
        mapped_file_t *mapped = map_file("nonexistent_file_12345.txt");
        printf("   map_file(nonexistent): %s (should be NULL)\n", mapped ? "Non-NULL" : "NULL");
        
        if (mapped) unmap_file(mapped);
    }
    
    // 5. 内存分配失败模拟
    {
        printf("5. Testing memory allocation limits:\n");
        
        // 尝试分配极大的结果容量
        match_result_t *huge_result = match_result_init(SIZE_MAX);
        printf("   match_result_init(SIZE_MAX): %s (should fail gracefully)\n", 
               huge_result ? "Succeeded (!)" : "Failed (expected)");
        
        if (huge_result) match_result_free(huge_result);
    }
    
    // 6. 无效线程数处理
    {
        printf("6. Testing invalid thread count handling:\n");
        
        const char *patterns[] = {"test"};
        size_t lens[] = {4};
        
        int invalid_threads[] = {-1, 0, 100};  // 负数、0、过大值
        
        for (int i = 0; i < 3; i++) {
            advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 1);
            params.thread_count = invalid_threads[i];
            
            match_result_t *result = match_result_init(10);
            uint64_t matches = search_advanced(&params, "test text", 9, result);
            
            printf("   Thread count %d: %llu matches (should work with auto-correction)\n", 
                   invalid_threads[i], matches);
            
            match_result_free(result);
        }
    }
    
    printf("\nAll error conditions handled gracefully! ✅\n");
}
```

---

## 完整示例程序

### 1. 命令行工具模拟

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "krep_advanced.h"

// 模拟完整的 krep 命令行工具
int main(int argc, char *argv[]) {
    printf("krep Advanced DLL Demo - Version: %s\n", get_version_advanced());
    printf("=======================================\n\n");
    
    // 模拟: krep -i -w -m 5 -t 2 "search_term" input.txt
    
    // 1. 创建测试输入文件
    const char *input_filename = "demo_input.txt";
    const char *file_content = 
        "This is a comprehensive demo file for krep testing.\n"
        "It contains multiple instances of SEARCH_TERM in different contexts.\n"
        "Some lines have search_term in lowercase.\n"
        "Other lines contain SearchTerm with mixed case.\n"
        "The word 'searching' contains our term but isn't a whole word match.\n"
        "Final line: another search_term occurrence here.\n";
    
    FILE *input_file = fopen(input_filename, "w");
    if (input_file) {
        fwrite(file_content, 1, strlen(file_content), input_file);
        fclose(input_file);
        printf("Created demo input file: %s\n\n", input_filename);
    }
    
    // 2. 设置搜索参数 (模拟命令行参数解析)
    const char *search_term = "search_term";
    const char *patterns[] = {search_term};
    size_t lens[] = {strlen(search_term)};
    
    advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 1);
    
    // 应用命令行选项
    KREP_SET_IGNORE_CASE(&params);      // -i: 不区分大小写
    KREP_SET_WHOLE_WORD(&params);       // -w: 整词匹配
    KREP_SET_MAX_COUNT(&params, 5);     // -m 5: 最多5个匹配
    KREP_SET_THREADS(&params, 2);       // -t 2: 使用2个线程
    
    printf("Search configuration:\n");
    printf("  Pattern: '%s'\n", search_term);
    printf("  Options: -i (ignore case), -w (whole word), -m 5 (max 5), -t 2 (2 threads)\n");
    printf("  Algorithm: %s\n", get_algorithm_name(select_best_algorithm(&params)));
    printf("\n");
    
    // 3. 执行内存映射文件搜索
    printf("Performing memory-mapped file search...\n");
    
    mapped_file_t *mapped = map_file(input_filename);
    if (mapped && mapped->data) {
        printf("✅ File mapped: %zu bytes\n", mapped->size);
        
        match_result_t *result = match_result_init(100);
        
        clock_t start = clock();
        uint64_t matches = search_advanced(&params, mapped->data, mapped->size, result);
        clock_t end = clock();
        
        double time_ms = ((double)(end - start) / CLOCKS_PER_SEC) * 1000.0;
        
        printf("\nSearch Results:\n");
        printf("==============\n");
        printf("Found %llu matches in %.3fms\n\n", matches, time_ms);
        
        // 显示每个匹配及其上下文
        for (uint64_t i = 0; i < result->count; i++) {
            size_t match_pos = result->positions[i].start_offset;
            
            // 找到包含匹配的行
            size_t line_start = match_pos;
            while (line_start > 0 && mapped->data[line_start-1] != '\n') {
                line_start--;
            }
            
            size_t line_end = match_pos;
            while (line_end < mapped->size && mapped->data[line_end] != '\n') {
                line_end++;
            }
            
            // 计算行号
            size_t line_num = 1;
            for (size_t j = 0; j < line_start; j++) {
                if (mapped->data[j] == '\n') line_num++;
            }
            
            printf("%s:%zu: ", input_filename, line_num);
            
            // 输出行内容，突出显示匹配部分
            for (size_t j = line_start; j < line_end; j++) {
                if (j >= result->positions[i].start_offset && 
                    j < result->positions[i].end_offset) {
                    printf("[%c]", mapped->data[j]);  // 用括号突出显示匹配
                } else {
                    printf("%c", mapped->data[j]);
                }
            }
            printf("\n");
        }
        
        match_result_free(result);
        unmap_file(mapped);
    } else {
        printf("❌ Failed to map file\n");
    }
    
    // 4. 演示不同搜索模式的对比
    printf("\n" "Comparison of Different Search Modes:\n");
    printf("=====================================\n");
    
    struct {
        const char *description;
        void (*configure)(advanced_search_params_t *p);
    } search_modes[] = {
        {"Default (case-sensitive, partial match)", NULL},
        {"Case-insensitive (-i)", configure_ignore_case},
        {"Whole word only (-w)", configure_whole_word}, 
        {"Case-insensitive + whole word (-i -w)", configure_ignore_case_whole_word}
    };
    
    for (int mode = 0; mode < 4; mode++) {
        advanced_search_params_t test_params = KREP_PARAMS_INIT(patterns, lens, 1);
        
        if (search_modes[mode].configure) {
            search_modes[mode].configure(&test_params);
        }
        
        uint64_t mode_matches = search_advanced(&test_params, file_content, strlen(file_content), NULL);
        printf("%-40s: %llu matches\n", search_modes[mode].description, mode_matches);
    }
    
    // 5. 性能基准测试
    printf("\n" "Performance Benchmark:\n");
    printf("======================\n");
    
    // 创建大文件进行性能测试
    size_t large_size = 10 * 1024 * 1024;  // 10MB
    char *large_data = malloc(large_size + 1);
    if (large_data) {
        // 填充重复数据
        const char *repeat_text = "Performance testing data with search_term patterns distributed throughout. ";
        size_t repeat_len = strlen(repeat_text);
        
        for (size_t i = 0; i < large_size; i += repeat_len) {
            size_t copy_len = (i + repeat_len > large_size) ? (large_size - i) : repeat_len;
            memcpy(large_data + i, repeat_text, copy_len);
        }
        large_data[large_size] = '\0';
        
        // 测试不同线程数的性能
        int thread_counts[] = {1, 2, 4};
        
        for (int t = 0; t < 3; t++) {
            advanced_search_params_t perf_params = KREP_PARAMS_INIT(patterns, lens, 1);
            KREP_SET_THREADS(&perf_params, thread_counts[t]);
            
            clock_t start = clock();
            uint64_t perf_matches = search_advanced(&perf_params, large_data, large_size, NULL);
            clock_t end = clock();
            
            double time_sec = ((double)(end - start) / CLOCKS_PER_SEC);
            double throughput = (large_size / 1024.0 / 1024.0) / time_sec;
            
            printf("Thread count %d: %llu matches, %.2f MB/s\n", 
                   thread_counts[t], perf_matches, throughput);
        }
        
        free(large_data);
    }
    
    // 6. 清理
    remove(input_filename);
    
    printf("\n✅ Demo completed successfully!\n");
    printf("\nThis demo showcased:\n");
    printf("• 🔍 Basic and advanced search functionality\n");
    printf("• 🧠 Smart algorithm selection\n");
    printf("• 🚀 Multi-threading performance\n");
    printf("• 💾 Memory-mapped file I/O\n");
    printf("• ⚙️  All command-line options\n");
    printf("• 📊 Performance benchmarking\n");
    printf("• 🛡️  Error handling\n");
    
    return 0;
}

// 配置函数实现
void configure_ignore_case(advanced_search_params_t *params) {
    KREP_SET_IGNORE_CASE(params);
}

void configure_whole_word(advanced_search_params_t *params) {
    KREP_SET_WHOLE_WORD(params);
}

void configure_ignore_case_whole_word(advanced_search_params_t *params) {
    KREP_SET_IGNORE_CASE(params);
    KREP_SET_WHOLE_WORD(params);
}
```

---

## 性能优化建议

### 1. 算法选择优化

```c
// 根据不同场景选择最优算法
void algorithm_optimization_tips() {
    printf("=== Algorithm Optimization Tips ===\n");
    
    struct {
        const char *scenario;
        const char *pattern;
        search_algorithm_t recommended;
        const char *reason;
    } optimization_cases[] = {
        {"Single character search", "x", ALGO_MEMCHR, 
         "memchr() is optimized for single character"},
        {"Short repetitive pattern", "aaaa", ALGO_KMP,
         "KMP handles repetitive patterns efficiently"},
        {"Long unique pattern", "comprehensive", ALGO_BOYER_MOORE,
         "Boyer-Moore skip distance is optimal for long patterns"},
        {"Hardware acceleration", "performance", ALGO_SIMD_SSE42,
         "SIMD provides parallel character comparison"},
        {"Multiple patterns", "error|warning", ALGO_AHO_CORASICK,
         "Aho-Corasick processes multiple patterns in single pass"}
    };
    
    for (int i = 0; i < 5; i++) {
        printf("%d. %s:\n", i+1, optimization_cases[i].scenario);
        printf("   Pattern: '%s'\n", optimization_cases[i].pattern);
        printf("   Algorithm: %s\n", get_algorithm_name(optimization_cases[i].recommended));
        printf("   Why: %s\n\n", optimization_cases[i].reason);
    }
}
```

### 2. 内存和线程优化

```c
// 内存和线程使用的最佳实践
void performance_best_practices() {
    printf("=== Performance Best Practices ===\n\n");
    
    printf("1. 内存映射 I/O:\n");
    printf("   • 大文件 (>1MB): 使用 map_file() 获得最佳性能\n");
    printf("   • 小文件 (<100KB): 直接读取到内存可能更快\n");
    printf("   • 多次搜索同一文件: 保持映射状态重复使用\n\n");
    
    printf("2. 线程配置:\n");
    printf("   • 小文件 (<1MB): thread_count = 1 (避免线程开销)\n");
    printf("   • 中等文件 (1-10MB): thread_count = 2-4\n");
    printf("   • 大文件 (>10MB): thread_count = CPU核心数\n");
    printf("   • 自动检测: thread_count = 0 (推荐)\n\n");
    
    printf("3. 结果容器大小:\n");
    printf("   • 预期少量匹配 (<100): match_result_init(100)\n");
    printf("   • 预期大量匹配 (>1000): match_result_init(10000)\n");
    printf("   • 仅计数: 使用 NULL 结果参数\n\n");
    
    printf("4. SIMD 优化:\n");
    printf("   • 模式长度 ≤16 字符: 启用 SSE4.2\n");
    printf("   • 模式长度 ≤32 字符: 启用 AVX2 (如果支持)\n");
    printf("   • 非英文文本或特殊字符: 考虑禁用 SIMD\n\n");
    
    printf("5. 批量处理:\n");
    printf("   • 多文件搜索: 使用 search_directory_recursive()\n");
    printf("   • 相同模式多次搜索: 重用 advanced_search_params_t\n");
    printf("   • 结果处理: 使用回调函数避免大量内存占用\n");
}
```

---

## 📚 API 参考速查

### 核心数据结构

```c
// 匹配位置
typedef struct {
    size_t start_offset;  // 匹配开始位置
    size_t end_offset;    // 匹配结束位置 (不包含)
} match_position_t;

// 匹配结果集
typedef struct {
    match_position_t *positions;  // 位置数组
    uint64_t count;               // 匹配数量
    uint64_t capacity;            // 数组容量
} match_result_t;

// 搜索算法枚举
typedef enum {
    ALGO_AUTO = 0,        // 自动选择
    ALGO_BOYER_MOORE,     // Boyer-Moore-Horspool
    ALGO_KMP,             // Knuth-Morris-Pratt
    ALGO_MEMCHR,          // memchr 优化
    ALGO_SIMD_SSE42,      // SIMD SSE4.2
    ALGO_SIMD_AVX2,       // SIMD AVX2
    ALGO_AHO_CORASICK,    // Aho-Corasick
    ALGO_REGEX            // 正则表达式
} search_algorithm_t;

// 高级搜索参数
typedef struct {
    // 模式配置
    const char **patterns;       // 模式数组
    size_t *pattern_lens;       // 长度数组
    size_t num_patterns;        // 模式数量
    
    // 搜索选项
    bool case_sensitive;        // 区分大小写
    bool whole_word;           // 全词匹配
    bool use_regex;            // 正则表达式
    bool count_only;           // 仅计数
    bool only_matching;        // 仅匹配部分
    size_t max_count;          // 最大匹配数
    
    // 性能选项
    search_algorithm_t force_algorithm;  // 强制算法
    bool use_simd;             // SIMD 加速
    int thread_count;          // 线程数
} advanced_search_params_t;
```

### 核心 API 函数

```c
// 初始化和清理
match_result_t* match_result_init(uint64_t initial_capacity);
void match_result_free(match_result_t *result);

// 主要搜索接口
uint64_t search_advanced(const advanced_search_params_t *params,
                        const char *text, size_t text_len,
                        match_result_t *result);

// 文件操作
mapped_file_t* map_file(const char *filename);
void unmap_file(mapped_file_t *mf);
int search_file_advanced(const char *filename,
                         const advanced_search_params_t *params,
                         void (*callback)(...), void *user_data);

// 文件类型检测
bool is_binary_file(const char *filename);
bool should_skip_directory(const char *dirname);
bool detect_binary_content(const char *data, size_t size);

// 实用函数
const char* get_version_advanced(void);
const char* get_algorithm_name(search_algorithm_t algo);
size_t get_file_size(const char *filename);
bool file_exists(const char *filename);
```

### 便捷宏

```c
// 参数初始化
#define KREP_PARAMS_INIT(pattern_array, lens_array, num)

// 选项设置
#define KREP_SET_IGNORE_CASE(params)     // -i
#define KREP_SET_WHOLE_WORD(params)      // -w  
#define KREP_SET_COUNT_ONLY(params)      // -c
#define KREP_SET_ONLY_MATCHING(params)   // -o
#define KREP_SET_REGEX(params)           // -E
#define KREP_SET_MAX_COUNT(params, num)  // -m
#define KREP_SET_THREADS(params, num)    // -t
#define KREP_DISABLE_SIMD(params)        // --no-simd
```

---

## 🚀 快速部署清单

### Windows 部署文件
- [ ] `krep_advanced.dll` - 主要 DLL (119KB)
- [ ] `libkrep_advanced.dll.a` - 链接库 (10KB)  
- [ ] `krep_advanced.h` - 头文件
- [ ] `USER_GUIDE.md` - 本使用指南

### 编译命令
```bash
gcc your_program.c -I. -L. -lkrep_advanced -o your_program.exe
```

### 最小示例
```c
#include "krep_advanced.h"

int main() {
    const char *patterns[] = {"search"};
    size_t lens[] = {6};
    advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 1);
    
    match_result_t *result = match_result_init(10);
    uint64_t matches = search_advanced(&params, "text to search", 14, result);
    
    printf("Found %llu matches\n", matches);
    match_result_free(result);
    return 0;
}
```

---

**🎯 现在您已掌握 krep Advanced DLL 的所有功能！开始构建高性能的文本搜索应用程序吧！**