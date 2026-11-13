#ifndef GTOS_CONTENT_CONDITION_VARIABLE_H_
#define GTOS_CONTENT_CONDITION_VARIABLE_H_

#include <__external_threading>

_LIBCPP_BEGIN_NAMESPACE_STD

// Wait on the same native condition state with an absolute monotonic deadline.
_LIBCPP_NO_THREAD_SAFETY_ANALYSIS
int __libcpp_condvar_timedwait_monotonic(__libcpp_condvar_t*, __libcpp_mutex_t*,
                                        const timespec*);

_LIBCPP_END_NAMESPACE_STD

#endif  // GTOS_CONTENT_CONDITION_VARIABLE_H_
