//
// Created by root on 10/10/24.
//

#ifndef TD_UTILS_H
#define TD_UTILS_H
#pragma once
#include <cstring>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cctype>
#include <atomic>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <chrono>
#include <thread>

namespace TRADER_UTILS
{
    template<typename HeaderType, size_t Capacity>
    class SPSCVarQueue {
    private:
        struct MsgHeader {
            size_t size;
            HeaderType header;
        };

        alignas(64) std::array<char, Capacity> buffer;
        alignas(64) volatile size_t write_idx;
        alignas(64) volatile size_t read_idx;

    public:
        SPSCVarQueue() : write_idx(0), read_idx(0) {}

        bool push(const HeaderType &header, const void *data, size_t data_size) {
            size_t total_size = sizeof(MsgHeader) + data_size;
            size_t current_write = write_idx;
            size_t current_read = read_idx;

            // 计算可用空间
            size_t available_space;
            if (current_write >= current_read) {
                available_space = Capacity - (current_write - current_read);
            } else {
                available_space = current_read - current_write;
            }

            // 检查是否有足够的空间
            if (available_space <= total_size) {
                return false;  // 空间不足，队列满
            }

            // 计算新的写入位置
            size_t next_write = (current_write + total_size) % Capacity;

            // 写入消息头
            MsgHeader msg_header{data_size, header};
            if (current_write + sizeof(MsgHeader) <= Capacity) {
                std::memcpy(&buffer[current_write], &msg_header, sizeof(MsgHeader));
            } else {
                size_t first_part = Capacity - current_write;
                std::memcpy(&buffer[current_write], &msg_header, first_part);
                std::memcpy(&buffer[0], reinterpret_cast<const char *>(&msg_header) + first_part,
                            sizeof(MsgHeader) - first_part);
            }

            // 写入数据（如果有的话）
            if (data_size > 0) {
                size_t data_start = (current_write + sizeof(MsgHeader)) % Capacity;
                if (data_start + data_size <= Capacity) {
                    std::memcpy(&buffer[data_start], data, data_size);
                } else {
                    size_t first_part = Capacity - data_start;
                    std::memcpy(&buffer[data_start], data, first_part);
                    std::memcpy(&buffer[0], static_cast<const char *>(data) + first_part, data_size - first_part);
                }
            }

            std::atomic_thread_fence(std::memory_order_release);
            write_idx = next_write;
            return true;
        }

        bool pop(HeaderType &header, void *data, size_t &data_size) {
            size_t current_read = read_idx;
            size_t current_write = write_idx;

            if (current_read == current_write)
                return false;  // 队列空

            std::atomic_thread_fence(std::memory_order_acquire);

            MsgHeader msg_header;
            size_t header_end = (current_read + sizeof(MsgHeader)) % Capacity;
            if (header_end < current_read) {
                // 头部跨越了缓冲区末尾
                size_t first_part = Capacity - current_read;
                std::memcpy(&msg_header, &buffer[current_read], first_part);
                std::memcpy(reinterpret_cast<char*>(&msg_header) + first_part, &buffer[0], sizeof(MsgHeader) - first_part);
            } else {
                // 头部在连续内存中
                std::memcpy(&msg_header, &buffer[current_read], sizeof(MsgHeader));
            }

            header = msg_header.header;
            data_size = msg_header.size;

            size_t data_start = header_end;
            size_t data_end = (data_start + data_size) % Capacity;

            if (data_end <= data_start && data_size > 0) {
                // 数据跨越了缓冲区末尾
                size_t first_part = Capacity - data_start;
                std::memcpy(data, &buffer[data_start], first_part);
                std::memcpy(static_cast<char*>(data) + first_part, &buffer[0], data_size - first_part);
            } else {
                // 数据在连续内存中
                std::memcpy(data, &buffer[data_start], data_size);
            }

            std::atomic_thread_fence(std::memory_order_release);
            read_idx = data_end;
            return true;
        }
    };
    class TSCNS
    {
    public:
        static const int64_t NsPerSec = 1000000000;//1e9

        void init(int64_t init_calibrate_ns = 20000000, int64_t calibrate_interval_ns = 3 * NsPerSec) {
            calibate_interval_ns_ = calibrate_interval_ns;// 校准时钟的周期，默认3s
            int64_t base_tsc, base_ns;
            syncTime(base_tsc, base_ns);//同步tsc和系统时钟
            int64_t expire_ns = base_ns + init_calibrate_ns;
            while (rdsysns() < expire_ns) std::this_thread::yield();
            int64_t delayed_tsc, delayed_ns;
            syncTime(delayed_tsc, delayed_ns);
            double init_ns_per_tsc = (double)(delayed_ns - base_ns) / (delayed_tsc - base_tsc);
            saveParam(base_tsc, base_ns, base_ns, init_ns_per_tsc);
        }

        void calibrate() {
            if (rdtsc() < next_calibrate_tsc_) return;
            int64_t tsc, ns;
            syncTime(tsc, ns);
            int64_t calulated_ns = tsc2ns(tsc);
            int64_t ns_err = calulated_ns - ns;
            int64_t expected_err_at_next_calibration =
                    ns_err + (ns_err - base_ns_err_) * calibate_interval_ns_ / (ns - base_ns_ + base_ns_err_);
            double new_ns_per_tsc =
                    ns_per_tsc_ * (1.0 - (double)expected_err_at_next_calibration / calibate_interval_ns_);
            saveParam(tsc, calulated_ns, ns, new_ns_per_tsc);
        }

        static inline int64_t rdtsc() {
#ifdef _MSC_VER
            return __rdtsc();
#elif defined(__i386__) || defined(__x86_64__) || defined(__amd64__)
            return __builtin_ia32_rdtsc();
#else
            return rdsysns();
#endif
        }

        inline int64_t tsc2ns(int64_t tsc) const {
            while (true) {
                uint32_t before_seq = param_seq_.load(std::memory_order_acquire) & ~1;
                std::atomic_signal_fence(std::memory_order_acq_rel);
                int64_t ns = base_ns_ + (int64_t)((tsc - base_tsc_) * ns_per_tsc_);
                std::atomic_signal_fence(std::memory_order_acq_rel);
                uint32_t after_seq = param_seq_.load(std::memory_order_acquire);
                if (before_seq == after_seq) return ns;
            }
        }

        inline int64_t rdns() const { return tsc2ns(rdtsc()); }

        static inline int64_t rdsysns() {
            using namespace std::chrono;
            return duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
        }

        double getTscGhz() const { return 1.0 / ns_per_tsc_; }

        // Linux kernel sync time by finding the first trial with tsc diff < 50000
        // We try several times and return the one with the mininum tsc diff.
        // Note that MSVC has a 100ns resolution clock, so we need to combine those ns with the same
        // value, and drop the first and the last value as they may not scan a full 100ns range
        static void syncTime(int64_t& tsc_out, int64_t& ns_out) {
#ifdef _MSC_VER
            const int N = 15;
#else
            const int N = 3;
#endif
            int64_t tsc[N + 1];
            int64_t ns[N + 1];

            tsc[0] = rdtsc();
            for (int i = 1; i <= N; i++) {
                ns[i] = rdsysns();
                tsc[i] = rdtsc();
            }

#ifdef _MSC_VER
            int j = 1;
        for (int i = 2; i <= N; i++) {
            if (ns[i] == ns[i - 1]) continue;
            tsc[j - 1] = tsc[i - 1];
            ns[j++] = ns[i];
        }
        j--;
#else
            int j = N + 1;
#endif

            int best = 1;
            for (int i = 2; i < j; i++) {
                if (tsc[i] - tsc[i - 1] < tsc[best] - tsc[best - 1]) best = i;
            }
            tsc_out = (tsc[best] + tsc[best - 1]) >> 1;
            ns_out = ns[best];
        }

        void saveParam(int64_t base_tsc, int64_t base_ns, int64_t sys_ns, double new_ns_per_tsc) {
            base_ns_err_ = base_ns - sys_ns;
            next_calibrate_tsc_ = base_tsc + (int64_t)((calibate_interval_ns_ - 1000) / new_ns_per_tsc);
            uint32_t seq = param_seq_.load(std::memory_order_relaxed);
            param_seq_.store(++seq, std::memory_order_release);
            std::atomic_signal_fence(std::memory_order_acq_rel);
            base_tsc_ = base_tsc;
            base_ns_ = base_ns;
            ns_per_tsc_ = new_ns_per_tsc;
            std::atomic_signal_fence(std::memory_order_acq_rel);
            param_seq_.store(++seq, std::memory_order_release);
        }

        alignas(64) std::atomic<uint32_t> param_seq_ = 0;
        double ns_per_tsc_;
        int64_t base_tsc_;
        int64_t base_ns_;
        int64_t calibate_interval_ns_;
        int64_t base_ns_err_;
        int64_t next_calibrate_tsc_;
    };
}

#endif //TD_UTILS_H
