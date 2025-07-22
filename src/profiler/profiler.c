#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <x86intrin.h>

#include "profiler_internal.h"

/**
 * Frequency of the OS timer in ticks per second. In Linux return 1000000,
 * indicating that the OS timer is assumed to have a resolution of microseconds
 * (1,000,000 microseconds in a second)
 */
uint64_t get_os_timer_freq() { return 1000000; }

uint64_t read_os_current_time() {
    struct timeval value;
    gettimeofday(&value, 0); /** Read the current time from the OS */

    uint64_t result = get_os_timer_freq() * (uint64_t)value.tv_sec + (uint64_t)value.tv_usec; /** OS current time in microseconds */
    return result;
}

uint64_t cpu_cycles_elapsed_since_processor_started() { return __rdtsc(); } /** Get CPU cycles that have elapsed since the processor started */

void print_time_elapsed(char const *label, uint64_t elapsed_cpu_cycles, uint64_t begin, uint64_t end) {
    uint64_t elapsed = end - begin;
    double percent = 100.0 * ((double)elapsed / (double)elapsed_cpu_cycles);
    printf("%s: %lu (%.2f%%)\n", label, elapsed, percent);
}

void print_time_elapsed2(uint64_t total_tsc_elapsed, ProfileAnchor *anchor) {
    uint64_t elapsed = anchor->tsc_elapsed;
    double percent = 100.0 * ((double)elapsed / (double)total_tsc_elapsed);
    printf("  %s[%lu]: %lu (%.2f%%)\n", anchor->label, anchor->hit_count, elapsed, percent);
}

/**
 * Estimate the CPU timer frequency (TSC frequency) in ticks per second.
 * It does this by measuring how many CPU cycles elapse during one second of OS time.
 */
uint64_t estimate_cpu_timer_freq() {
    uint64_t os_freq = get_os_timer_freq();

    uint64_t cpu_start = cpu_cycles_elapsed_since_processor_started();

    uint64_t os_start = read_os_current_time();
    uint64_t os_end = 0;
    uint64_t os_ticks_elapsed = 0;
    while (os_ticks_elapsed < os_freq) {
        os_end = read_os_current_time();
        os_ticks_elapsed = os_end - os_start;
    }

    uint64_t cpu_end = cpu_cycles_elapsed_since_processor_started();
    uint64_t cpu_freq = cpu_end - cpu_start;

    return cpu_freq;
}

#ifndef READ_BLOCK_TIMER
#define READ_BLOCK_TIMER cpu_cycles_elapsed_since_processor_started
#endif

Profiler global_profiler;

void ark_begin_profile() { global_profiler.start_tsc = READ_BLOCK_TIMER(); }

void ark_end_and_print_profile() {
    global_profiler.end_tsc = cpu_cycles_elapsed_since_processor_started();
    uint64_t cpu_freq = estimate_cpu_timer_freq();

    uint64_t elapsed_cpu_cycles = global_profiler.end_tsc - global_profiler.start_tsc;

    if (cpu_freq) {
        printf("\nTotal time: %0.4fms (Estimated CPU %.2fGHz or %luHz)\n", 1000.0 * (double)elapsed_cpu_cycles / (double)cpu_freq, (double)cpu_freq / 1000000000.0, cpu_freq);
    }

    uint32_t anchor_index;

    for (anchor_index = 0; anchor_index < array_count(global_profiler.anchors); ++anchor_index) {
        ProfileAnchor *anchor = global_profiler.anchors + anchor_index;

        if (anchor->tsc_elapsed) {
            print_time_elapsed2(elapsed_cpu_cycles, anchor);
        }
    }

    memset(&global_profiler, 0, sizeof(global_profiler));
}

ProfileBlock profile_block_scope_start(const char *label, uint32_t anchor_index) {
    ProfileBlock block = {0};

    block.anchor_index = anchor_index;
    block.label = label;
    block.start_tsc = READ_BLOCK_TIMER();

    return block;
}

void profile_block_scope_end(ProfileBlock *block) {
    uint64_t elapsed = READ_BLOCK_TIMER() - block->start_tsc;

    ProfileAnchor *anchor = global_profiler.anchors + block->anchor_index;

    anchor->tsc_elapsed += elapsed;
    ++anchor->hit_count;

    anchor->label = block->label;
}
