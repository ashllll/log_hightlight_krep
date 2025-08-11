/* performance_demo.c - 性能演示示例
 * 
 * 展示 krep Advanced DLL 的多线程和 SIMD 性能特性
 * 编译命令: gcc performance_demo.c -I../include -L../lib -lkrep_advanced -o performance_demo.exe
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "../include/krep_advanced.h"

// 生成测试数据
char* generate_test_data(size_t size_mb) {
    size_t total_size = size_mb * 1024 * 1024;
    char *data = (char*)malloc(total_size + 1);
    if (!data) return NULL;
    
    const char *base_text = "This is performance test data with search patterns and target keywords distributed throughout the content. ";
    size_t base_len = strlen(base_text);
    
    for (size_t i = 0; i < total_size; i += base_len) {
        size_t copy_len = (i + base_len > total_size) ? (total_size - i) : base_len;
        memcpy(data + i, base_text, copy_len);
    }
    data[total_size] = '\0';
    
    return data;
}

// 测量搜索性能
double measure_search_performance(const advanced_search_params_t *params, 
                                 const char *data, size_t data_size,
                                 const char *test_name) {
    match_result_t *result = match_result_init(10000);
    if (!result) return -1.0;
    
    clock_t start = clock();
    uint64_t matches = search_advanced(params, data, data_size, result);
    clock_t end = clock();
    
    double time_ms = ((double)(end - start) / CLOCKS_PER_SEC) * 1000.0;
    double throughput = (data_size / 1024.0 / 1024.0) / (time_ms / 1000.0);
    
    printf("%-25s: %llu 匹配, %.2fms, %.2f MB/s\n", 
           test_name, (unsigned long long)matches, time_ms, throughput);
    
    match_result_free(result);
    return throughput;
}

int main() {
    printf("krep Advanced DLL - 性能演示\n");
    printf("============================\n\n");
    
    // 1. 生成测试数据
    printf("📊 生成性能测试数据...\n");
    size_t test_size_mb = 5; // 5MB 测试数据
    char *test_data = generate_test_data(test_size_mb);
    
    if (!test_data) {
        printf("❌ 无法分配测试数据内存\n");
        return 1;
    }
    
    size_t data_size = test_size_mb * 1024 * 1024;
    printf("✅ 生成 %zuMB 测试数据\n\n", test_size_mb);
    
    // 2. 算法性能对比
    printf("🔍 算法性能对比 (%zuMB 数据):\n", test_size_mb);
    printf("=================================\n");
    
    const char *patterns[] = {"performance"};
    size_t lens[] = {11};
    
    // 自动算法选择
    advanced_search_params_t auto_params = KREP_PARAMS_INIT(patterns, lens, 1);
    auto_params.force_algorithm = ALGO_AUTO;
    measure_search_performance(&auto_params, test_data, data_size, "自动算法选择");
    
    // Boyer-Moore 算法
    advanced_search_params_t bm_params = KREP_PARAMS_INIT(patterns, lens, 1);
    bm_params.force_algorithm = ALGO_BOYER_MOORE;
    measure_search_performance(&bm_params, test_data, data_size, "Boyer-Moore-Horspool");
    
    // KMP 算法
    advanced_search_params_t kmp_params = KREP_PARAMS_INIT(patterns, lens, 1);
    kmp_params.force_algorithm = ALGO_KMP;
    measure_search_performance(&kmp_params, test_data, data_size, "Knuth-Morris-Pratt");
    
    // 3. 多线程性能测试
    printf("\n🚀 多线程性能测试:\n");
    printf("==================\n");
    
    int thread_counts[] = {1, 2, 4, 8};
    double baseline_perf = 0.0;
    
    for (int i = 0; i < 4; i++) {
        advanced_search_params_t thread_params = KREP_PARAMS_INIT(patterns, lens, 1);
        KREP_SET_THREADS(&thread_params, thread_counts[i]);
        
        char test_name[50];
        snprintf(test_name, sizeof(test_name), "%d 线程", thread_counts[i]);
        
        double perf = measure_search_performance(&thread_params, test_data, data_size, test_name);
        
        if (i == 0) {
            baseline_perf = perf;
        } else if (baseline_perf > 0) {
            double speedup = perf / baseline_perf;
            printf("                           加速比: %.2fx\n", speedup);
        }
    }
    
    // 4. SIMD 性能对比
    printf("\n⚡ SIMD 加速对比:\n");
    printf("================\n");
    
    // SIMD 启用
    advanced_search_params_t simd_on_params = KREP_PARAMS_INIT(patterns, lens, 1);
    simd_on_params.use_simd = true;
    double simd_perf = measure_search_performance(&simd_on_params, test_data, data_size, "SIMD 启用");
    
    // SIMD 禁用
    advanced_search_params_t simd_off_params = KREP_PARAMS_INIT(patterns, lens, 1);
    KREP_DISABLE_SIMD(&simd_off_params);
    double no_simd_perf = measure_search_performance(&simd_off_params, test_data, data_size, "SIMD 禁用");
    
    if (no_simd_perf > 0 && simd_perf > 0) {
        double simd_speedup = simd_perf / no_simd_perf;
        printf("                           SIMD 加速比: %.2fx\n", simd_speedup);
    }
    
    // 5. 不同模式串长度的性能
    printf("\n📏 不同模式串长度性能:\n");
    printf("======================\n");
    
    struct {
        const char *pattern;
        const char *description;
    } pattern_tests[] = {
        {"a", "单字符 (memchr 优化)"},
        {"test", "短模式 (4字符)"},
        {"performance", "中等长度 (11字符)"},
        {"comprehensive_performance_test", "长模式 (30字符)"}
    };
    
    for (int i = 0; i < 4; i++) {
        const char *test_patterns[] = {pattern_tests[i].pattern};
        size_t test_lens[] = {strlen(pattern_tests[i].pattern)};
        
        advanced_search_params_t pattern_params = KREP_PARAMS_INIT(test_patterns, test_lens, 1);
        measure_search_performance(&pattern_params, test_data, data_size, pattern_tests[i].description);
    }
    
    // 6. 内存使用效率测试
    printf("\n💾 内存使用效率:\n");
    printf("================\n");
    
    // 测试不同结果容器大小的影响
    const char *mem_patterns[] = {"test"};
    size_t mem_lens[] = {4};
    advanced_search_params_t mem_params = KREP_PARAMS_INIT(mem_patterns, mem_lens, 1);
    
    size_t capacities[] = {10, 100, 1000, 10000};
    
    for (int i = 0; i < 4; i++) {
        match_result_t *result = match_result_init(capacities[i]);
        if (result) {
            clock_t start = clock();
            uint64_t matches = search_advanced(&mem_params, test_data, data_size, result);
            clock_t end = clock();
            
            double time_ms = ((double)(end - start) / CLOCKS_PER_SEC) * 1000.0;
            
            printf("容器容量 %zu     : %llu 匹配, %.2fms\n", 
                   capacities[i], (unsigned long long)matches, time_ms);
            
            match_result_free(result);
        }
    }
    
    // 7. 仅计数模式性能 (无内存分配)
    printf("\n🔢 仅计数模式性能:\n");
    printf("==================\n");
    
    advanced_search_params_t count_params = KREP_PARAMS_INIT(patterns, lens, 1);
    KREP_SET_COUNT_ONLY(&count_params);
    
    clock_t start = clock();
    uint64_t count = search_advanced(&count_params, test_data, data_size, NULL);
    clock_t end = clock();
    
    double time_ms = ((double)(end - start) / CLOCKS_PER_SEC) * 1000.0;
    double throughput = (data_size / 1024.0 / 1024.0) / (time_ms / 1000.0);
    
    printf("仅计数 (无内存分配)    : %llu 匹配, %.2fms, %.2f MB/s\n",
           (unsigned long long)count, time_ms, throughput);
    
    // 8. 清理资源
    free(test_data);
    
    printf("\n✅ 性能演示完成!\n");
    printf("\n📈 性能优化建议:\n");
    printf("   • 大文件 (>1MB): 启用多线程获得最佳性能\n");
    printf("   • 支持 SIMD 的 CPU: 启用硬件加速\n");
    printf("   • 单字符搜索: 自动使用 memchr 优化\n");
    printf("   • 仅需要计数: 使用 COUNT_ONLY 模式节省内存\n");
    printf("   • 预期大量匹配: 预分配足够大的结果容器\n");
    
    return 0;
}