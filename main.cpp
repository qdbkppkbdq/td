#include <iostream>
#include <cassert>
#include "test.h"
#include "utils.h"

struct TestHeader {
    uint32_t id;
    uint64_t timestamp;
};
// 性能测试
void test_performance() {
    std::cout << "Running performance test..." << std::endl;
    TRADER_UTILS::SPSCVarQueue<TestHeader, 1024 * 1024> queue;
    const int NUM_MESSAGES = 1000000;

    auto start = std::chrono::high_resolution_clock::now();

    std::thread producer([&]() {
        for (int i = 0; i < NUM_MESSAGES; ++i) {
            TestHeader header{static_cast<uint32_t>(i), static_cast<uint64_t>(i)};
            while (!queue.push(header, &i, sizeof(i))) {
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&]() {
        TestHeader header{};
        int data;
        size_t size;
        for (int i = 0; i < NUM_MESSAGES; ++i) {
            while (!queue.pop(header, &data, size)) {
                std::this_thread::yield();
            }
            assert(header.id == i);
            assert(data == i);
        }
    });

    producer.join();
    consumer.join();

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "Performance test passed. Processed " << NUM_MESSAGES << " messages in "
              << duration.count() << " ms" << std::endl;
    std::cout << "Throughput: " << NUM_MESSAGES * 1000.0 / duration.count() << " messages/second" << std::endl;
}
int main() {
    std::cout << "Hello, World!" << add(1,2) << std::endl;
    test_performance();
    return 0;
}
