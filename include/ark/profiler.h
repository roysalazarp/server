#ifndef ARK_PROFILER_H
#define ARK_PROFILER_H

void ark_begin_profile();
void ark_end_and_print_profile();

#define ARK_TIME_BLOCK(name) TIME_BLOCK_LINE_HELPER(name, __LINE__)

#endif /* ARK_PROFILER_H */