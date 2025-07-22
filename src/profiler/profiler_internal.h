#ifndef ARK_PROFILER_INTERNAL_H
#define ARK_PROFILER_INTERNAL_H

#include "../../include/ark/shared.h"

/** IMPORTANT: The profiler does NOT support nested blocks yet */

typedef struct {
    uint64_t tsc_elapsed;
    uint64_t hit_count;
    const char *label;
} ProfileAnchor;

typedef struct {
    const char *label;
    uint64_t start_tsc;
    uint32_t anchor_index;
} ProfileBlock;

typedef struct {
    ProfileAnchor anchors[4096];
    uint64_t start_tsc;
    uint64_t end_tsc;
} Profiler;

ProfileBlock profile_block_scope_start(const char *label, uint32_t anchor_index);
void profile_block_scope_end(ProfileBlock *block);

/* clang-format off */
#define NameConcat2(A, B) A##B
#define NameConcat(A, B) NameConcat2(A, B)
#define profiler_end_of_compilation_unit static_assert(__COUNTER__ < array_count(global_profiler_anchors), "Number of profile points exceeds size of profiler::Anchors array")

#define TIME_BLOCK_LINE_HELPER(name, line) \
    ProfileBlock NameConcat(block, line) = profile_block_scope_start(name, __COUNTER__ + 1); \
    NameConcat(int first_time_, line) = 1; \
    for(; NameConcat(first_time_, line); profile_block_scope_end(&(NameConcat(block, line))), NameConcat(first_time_, line) = 0)

#define ARK_TIME_BLOCK(name) TIME_BLOCK_LINE_HELPER(name, __LINE__)
/* clang-format on */

extern Profiler global_profiler;

#endif /* ARK_PROFILER_INTERNAL_H */