/* file_search.c - 文件搜索示例
 * 
 * 展示如何使用 krep Advanced DLL 进行文件内搜索
 * 编译命令: gcc file_search.c -I../include -L../lib -lkrep_advanced -o file_search.exe
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../include/krep_advanced.h"

// 搜索结果回调函数
void search_result_callback(const char *filename, size_t line_number,
                           const char *line, const match_result_t *matches,
                           void *user_data) {
    int *total_matches = (int*)user_data;
    (*total_matches)++;
    
    printf("📄 %s:%zu: %s", filename, line_number, line);
    if (matches && matches->count > 0) {
        printf(" [找到 %llu 个匹配]", (unsigned long long)matches->count);
    }
    printf("\n");
}

int main() {
    printf("krep Advanced DLL - 文件搜索示例\n");
    printf("===============================\n\n");
    
    // 1. 创建测试文件
    const char *test_filename = "test_document.txt";
    const char *file_content = 
        "这是第一行包含 search 关键字的内容\n"
        "第二行有不同的文本内容\n"  
        "第三行再次包含 search 和 target 关键字\n"
        "最后一行包含 SEARCH 大写形式\n";
    
    FILE *test_file = fopen(test_filename, "w");
    if (!test_file) {
        printf("❌ 无法创建测试文件\n");
        return 1;
    }
    
    fwrite(file_content, 1, strlen(file_content), test_file);
    fclose(test_file);
    printf("✅ 创建测试文件: %s\n\n", test_filename);
    
    // 2. 设置搜索参数
    const char *patterns[] = {"search"};
    size_t lens[] = {6};
    advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 1);
    
    // 3. 演示内存映射文件搜索
    printf("1. 内存映射文件搜索:\n");
    printf("-------------------\n");
    
    mapped_file_t *mapped = map_file(test_filename);
    if (mapped && mapped->data && mapped->size > 0) {
        printf("✅ 文件成功映射到内存\n");
        printf("   文件大小: %zu 字节\n", mapped->size);
        printf("   映射状态: %s\n\n", mapped->is_mapped ? "已映射" : "未映射");
        
        // 在映射的内存中搜索 (区分大小写)
        match_result_t *result = match_result_init(100);
        if (result) {
            uint64_t matches = search_advanced(&params, mapped->data, mapped->size, result);
            printf("区分大小写搜索结果: %llu 个匹配\n", (unsigned long long)matches);
            
            // 显示匹配的上下文
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
                
                printf("  匹配 %llu: ", (unsigned long long)i+1);
                for (size_t j = line_start; j < line_end; j++) {
                    printf("%c", mapped->data[j]);
                }
                printf("\n");
            }
            
            match_result_free(result);
        }
        
        unmap_file(mapped);
    } else {
        printf("❌ 文件映射失败\n");
    }
    
    // 4. 演示不区分大小写的文件搜索
    printf("\n2. 不区分大小写搜索:\n");
    printf("------------------\n");
    
    KREP_SET_IGNORE_CASE(&params);
    
    int total_matches = 0;
    int result_code = search_file_advanced(test_filename, &params, 
                                          search_result_callback, &total_matches);
    
    switch (result_code) {
        case 0:
            printf("✅ 搜索完成，找到匹配\n");
            break;
        case 1:
            printf("ℹ️  搜索完成，未找到匹配\n");
            break;
        case 2:
            printf("❌ 搜索失败 (文件错误)\n");
            break;
    }
    
    printf("总匹配行数: %d\n", total_matches);
    
    // 5. 演示多模式搜索
    printf("\n3. 多模式搜索:\n");
    printf("------------\n");
    
    const char *multi_patterns[] = {"search", "target", "内容"};
    size_t multi_lens[] = {6, 6, 6}; // "内容" 是 UTF-8，实际字节数
    advanced_search_params_t multi_params = KREP_PARAMS_INIT(multi_patterns, multi_lens, 3);
    KREP_SET_IGNORE_CASE(&multi_params);
    
    total_matches = 0;
    result_code = search_file_advanced(test_filename, &multi_params,
                                      search_result_callback, &total_matches);
    
    printf("多模式搜索结果: %d 行包含匹配\n", total_matches);
    
    // 6. 清理测试文件
    remove(test_filename);
    printf("\n🗑️  清理测试文件\n");
    
    printf("\n✅ 文件搜索示例完成!\n");
    printf("\n💡 提示:\n");
    printf("   • 使用 map_file() 进行高效的大文件搜索\n");
    printf("   • search_file_advanced() 自动处理二进制文件检测\n");
    printf("   • 回调函数可以自定义结果处理逻辑\n");
    printf("   • 支持多模式并发搜索\n");
    
    return 0;
}