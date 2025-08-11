/* basic_search.c - 基础搜索示例
 * 
 * 展示如何使用 krep Advanced DLL 进行基本的文本搜索
 * 编译命令: gcc basic_search.c -I../include -L../lib -lkrep_advanced -o basic_search.exe
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../include/krep_advanced.h"

int main() {
    printf("krep Advanced DLL - 基础搜索示例\n");
    printf("================================\n\n");
    
    // 1. 定义搜索模式
    const char *patterns[] = {"hello", "world"};
    size_t lens[] = {5, 5};
    
    // 2. 创建搜索参数
    advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 2);
    
    // 3. 准备测试文本
    const char *text = "Hello world! This is a hello world example. "
                      "Another hello appears here with WORLD in caps.";
    
    // 4. 初始化结果容器
    match_result_t *result = match_result_init(100);
    if (!result) {
        printf("错误: 无法分配结果内存\n");
        return 1;
    }
    
    // 5. 执行基本搜索 (区分大小写)
    printf("1. 区分大小写搜索:\n");
    uint64_t matches = search_advanced(&params, text, strlen(text), result);
    printf("   找到 %llu 个匹配\n", (unsigned long long)matches);
    
    for (uint64_t i = 0; i < result->count; i++) {
        size_t start = result->positions[i].start_offset;
        size_t end = result->positions[i].end_offset;
        
        printf("   匹配 %llu: 位置 %zu-%zu, 内容: \"", 
               (unsigned long long)i+1, start, end-1);
        for (size_t j = start; j < end; j++) {
            printf("%c", text[j]);
        }
        printf("\"\n");
    }
    
    // 6. 不区分大小写搜索
    printf("\n2. 不区分大小写搜索:\n");
    result->count = 0; // 重置结果
    KREP_SET_IGNORE_CASE(&params);
    
    matches = search_advanced(&params, text, strlen(text), result);
    printf("   找到 %llu 个匹配\n", (unsigned long long)matches);
    
    for (uint64_t i = 0; i < result->count; i++) {
        size_t start = result->positions[i].start_offset;
        size_t end = result->positions[i].end_offset;
        
        printf("   匹配 %llu: 位置 %zu-%zu, 内容: \"",
               (unsigned long long)i+1, start, end-1);
        for (size_t j = start; j < end; j++) {
            printf("%c", text[j]);
        }
        printf("\"\n");
    }
    
    // 7. 仅计数模式
    printf("\n3. 仅计数模式:\n");
    KREP_SET_COUNT_ONLY(&params);
    uint64_t count = search_advanced(&params, text, strlen(text), NULL);
    printf("   总共找到 %llu 个匹配\n", (unsigned long long)count);
    
    // 8. 清理资源
    match_result_free(result);
    
    printf("\n✅ 基础搜索示例完成!\n");
    return 0;
}