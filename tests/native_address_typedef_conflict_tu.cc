/* Negative: a same-width redefinition is a type conflict, and the compiler says so.
 *
 * `socklen_t` is `unsigned long` in the canonical SDK header. On i386
 * `unsigned int` is the same width, so a `sizeof` check cannot tell them apart -
 * which is exactly why the previous revision's choice of `unsigned int` was wrong
 * and why "same size" is not an argument. This translation unit includes the
 * canonical header and then repeats the wrong typedef; it must fail with a
 * typedef redefinition, not compile.
 *
 * The runner also greps for that specific diagnostic, so a failure for any other
 * reason does not count as the negative having worked.
 */
#include <llvm-libc-types/socklen_t.h>

typedef unsigned int socklen_t;

int gtos_address_typedef_conflict_probe(void) {
    return (int)sizeof(socklen_t);
}
