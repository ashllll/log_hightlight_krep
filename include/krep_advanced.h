/* krep_advanced.h - Advanced Windows DLL header with all krep features
 *
 * Author: Based on krep by Davide Santangelo
 * Year: 2025
 */

#ifndef KREP_ADVANCED_H
#define KREP_ADVANCED_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef _WIN32
    #ifdef BUILDING_DLL
        #define KREP_API __declspec(dllexport)
    #else
        #define KREP_API __declspec(dllimport)
    #endif
#else
    #define KREP_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Data structures */
typedef struct {
    size_t start_offset;
    size_t end_offset;
} match_position_t;

typedef struct {
    match_position_t *positions;
    uint64_t count;
    uint64_t capacity;
} match_result_t;

/* Memory-mapped file structure */
typedef struct {
    char *data;
    size_t size;
#ifdef _WIN32
    void *file_handle;      // HANDLE file_handle
    void *mapping_handle;   // HANDLE mapping_handle  
#else
    int fd;
#endif
    bool is_mapped;
} mapped_file_t;

typedef enum {
    ALGO_AUTO = 0,              // 自动选择最优算法
    ALGO_BOYER_MOORE,           // Boyer-Moore-Horspool 算法
    ALGO_KMP,                   // Knuth-Morris-Pratt 算法
    ALGO_MEMCHR,               // memchr 优化 (单字符)
    ALGO_SIMD_SSE42,           // SIMD SSE4.2 加速
    ALGO_SIMD_AVX2,            // SIMD AVX2 加速
    ALGO_AHO_CORASICK,         // Aho-Corasick 多模式匹配
    ALGO_REGEX                 // 正则表达式引擎
} search_algorithm_t;

typedef struct {
    // 模式串配置
    const char **patterns;      // 模式串数组 (支持 -e 多模式)
    size_t *pattern_lens;      // 每个模式串的长度
    size_t num_patterns;       // 模式串数量
    
    // 搜索选项
    bool case_sensitive;       // 区分大小写 (默认 true, -i 设为 false)
    bool whole_word;          // 全词匹配 (-w 选项)
    bool use_regex;           // 使用正则表达式 (-E 选项)
    bool count_only;          // 仅计数 (-c 选项)
    bool only_matching;       // 仅显示匹配部分 (-o 选项)
    size_t max_count;         // 最大匹配数 (-m 选项, SIZE_MAX 为无限制)
    
    // 性能选项
    search_algorithm_t force_algorithm;  // 强制使用指定算法 (ALGO_AUTO 为自动)
    bool use_simd;            // 启用 SIMD 优化 (--no-simd 设为 false)
    int thread_count;         // 线程数 (-t 选项, 0 为自动检测)
} advanced_search_params_t;

/* API Functions - Core Search */

/**
 * @brief 高级搜索函数 - 支持所有 krep 功能
 * 
 * 特性:
 * - 智能算法选择 (Boyer-Moore-Horspool, KMP, memchr, SIMD)
 * - 多线程并行处理
 * - CPU 特性检测 (SSE4.2, AVX2)
 * - 多种搜索模式
 * 
 * @param params 搜索参数配置
 * @param text 要搜索的文本
 * @param text_len 文本长度
 * @param result 匹配结果存储 (可为 NULL 仅计数)
 * @return 找到的匹配数量
 */
KREP_API uint64_t search_advanced(const advanced_search_params_t *params,
                                 const char *text, size_t text_len,
                                 match_result_t *result);

/* API Functions - Match Result Management */

/**
 * @brief 初始化匹配结果结构
 * @param initial_capacity 初始容量
 * @return 匹配结果指针或 NULL
 */
KREP_API match_result_t* match_result_init(uint64_t initial_capacity);

/**
 * @brief 释放匹配结果结构
 * @param result 要释放的匹配结果
 */
KREP_API void match_result_free(match_result_t *result);

/* API Functions - File Type Detection (支持递归搜索 -r) */

/**
 * @brief 检测是否为二进制文件
 * 
 * 根据文件扩展名检测常见的二进制文件类型:
 * - 可执行文件: .exe, .dll, .so, .dylib
 * - 图像文件: .jpg, .png, .gif, .bmp
 * - 音视频文件: .mp3, .mp4, .avi, .mov  
 * - 压缩文件: .zip, .tar, .gz
 * - 文档文件: .pdf, .doc, .xls, .ppt
 * 
 * @param filename 文件名
 * @return true 如果是二进制文件
 */
KREP_API bool is_binary_file(const char *filename);

/**
 * @brief 检查是否应跳过目录
 * 
 * 自动跳过以下目录类型:
 * - 版本控制: .git, .svn, .hg
 * - 依赖目录: node_modules, __pycache__
 * - 虚拟环境: venv, .venv, env, .env
 * - 构建目录: build, dist, target, bin, obj
 * 
 * @param dirname 目录名
 * @return true 如果应该跳过
 */
KREP_API bool should_skip_directory(const char *dirname);

/* API Functions - Algorithm Information */

/**
 * @brief 获取算法名称
 * @param algo 算法枚举值
 * @return 算法名称字符串
 */
KREP_API const char* get_algorithm_name(search_algorithm_t algo);

/**
 * @brief 获取高级版本号
 * @return 版本字符串
 */
KREP_API const char* get_version_advanced(void);

/* API Functions - File I/O and Memory Mapping */

/**
 * @brief 内存映射文件
 * @param filename 文件名
 * @return 映射的文件结构或 NULL
 */
KREP_API mapped_file_t* map_file(const char *filename);

/**
 * @brief 取消文件映射
 * @param mf 映射的文件结构
 */
KREP_API void unmap_file(mapped_file_t *mf);

/**
 * @brief 检测文件是否包含二进制内容
 * @param data 文件数据
 * @param size 数据大小
 * @return true 如果包含二进制内容
 */
KREP_API bool detect_binary_content(const char *data, size_t size);

/**
 * @brief 在文件中搜索模式串
 * @param filename 文件名
 * @param params 搜索参数
 * @param callback 结果回调函数 (可以为 NULL)
 * @param user_data 用户数据
 * @return 0=成功, 1=无匹配, 2=错误
 */
KREP_API int search_file_advanced(const char *filename,
                                 const advanced_search_params_t *params,
                                 void (*callback)(const char*, size_t, const char*, const match_result_t*, void*),
                                 void *user_data);

/**
 * @brief 递归搜索目录
 * @param directory 目录路径
 * @param params 搜索参数
 * @param callback 结果回调函数 (可以为 NULL)
 * @param user_data 用户数据
 * @return 处理的错误数量
 */
KREP_API int search_directory_recursive(const char *directory,
                                       const advanced_search_params_t *params,
                                       void (*callback)(const char*, size_t, const char*, const match_result_t*, void*),
                                       void *user_data);

/**
 * @brief 获取文件大小
 * @param filename 文件名
 * @return 文件大小或 0
 */
KREP_API size_t get_file_size(const char *filename);

/**
 * @brief 检查文件是否存在且可读
 * @param filename 文件名
 * @return true 如果文件存在且可读
 */
KREP_API bool file_exists(const char *filename);

/* Convenience Macros for Common krep Command Line Options */

/**
 * 创建参数结构的便捷宏
 */
#define KREP_PARAMS_INIT(pattern_array, lens_array, num) { \
    .patterns = pattern_array, \
    .pattern_lens = lens_array, \
    .num_patterns = num, \
    .case_sensitive = true, \
    .whole_word = false, \
    .use_regex = false, \
    .count_only = false, \
    .only_matching = false, \
    .max_count = SIZE_MAX, \
    .force_algorithm = ALGO_AUTO, \
    .use_simd = true, \
    .thread_count = 0 \
}

/**
 * 模拟 krep -i (忽略大小写)
 */
#define KREP_SET_IGNORE_CASE(params) do { (params)->case_sensitive = false; } while(0)

/**
 * 模拟 krep -w (全词匹配) 
 */
#define KREP_SET_WHOLE_WORD(params) do { (params)->whole_word = true; } while(0)

/**
 * 模拟 krep -c (仅计数)
 */
#define KREP_SET_COUNT_ONLY(params) do { (params)->count_only = true; } while(0)

/**
 * 模拟 krep -o (仅匹配部分)
 */
#define KREP_SET_ONLY_MATCHING(params) do { (params)->only_matching = true; } while(0)

/**
 * 模拟 krep -E (正则表达式)
 */
#define KREP_SET_REGEX(params) do { (params)->use_regex = true; } while(0)

/**
 * 模拟 krep -m NUM (最大匹配数)
 */
#define KREP_SET_MAX_COUNT(params, num) do { (params)->max_count = num; } while(0)

/**
 * 模拟 krep -t NUM (线程数)
 */
#define KREP_SET_THREADS(params, num) do { (params)->thread_count = num; } while(0)

/**
 * 模拟 krep --no-simd (禁用SIMD)
 */
#define KREP_DISABLE_SIMD(params) do { (params)->use_simd = false; } while(0)

/* Usage Examples in Comments */

/*
// Example 1: 基本搜索 (模拟: krep "pattern" file.txt)
const char *patterns[] = {"pattern"};
size_t lens[] = {7};
advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 1);

match_result_t *result = match_result_init(100);
uint64_t matches = search_advanced(&params, text, text_len, result);
match_result_free(result);

// Example 2: 忽略大小写搜索 (模拟: krep -i "Pattern" file.txt)  
advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 1);
KREP_SET_IGNORE_CASE(&params);
uint64_t matches = search_advanced(&params, text, text_len, result);

// Example 3: 全词匹配 (模拟: krep -w "word" file.txt)
KREP_SET_WHOLE_WORD(&params);
uint64_t matches = search_advanced(&params, text, text_len, result);

// Example 4: 仅计数 (模拟: krep -c "pattern" file.txt)
KREP_SET_COUNT_ONLY(&params);  
uint64_t count = search_advanced(&params, text, text_len, NULL);

// Example 5: 多模式搜索 (模拟: krep -e "pat1" -e "pat2" file.txt)
const char *patterns[] = {"pat1", "pat2"};
size_t lens[] = {4, 4};
advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 2);
uint64_t matches = search_advanced(&params, text, text_len, result);

// Example 6: 限制匹配数和线程 (模拟: krep -m 10 -t 4 "pattern" file.txt)
KREP_SET_MAX_COUNT(&params, 10);
KREP_SET_THREADS(&params, 4);
uint64_t matches = search_advanced(&params, text, text_len, result);

// Example 7: 强制使用特定算法
params.force_algorithm = ALGO_KMP;  // 强制使用 KMP
uint64_t matches = search_advanced(&params, text, text_len, result);
*/

#ifdef __cplusplus
}
#endif

#endif /* KREP_ADVANCED_H */