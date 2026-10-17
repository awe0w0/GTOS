#ifndef GTOS_NATIVE_MATH_CASES_H
#define GTOS_NATIVE_MATH_CASES_H
#include "math-vectors.h"
extern "C" void NativeMathCallBits(double (*function)(double),const unsigned long long* input,unsigned long long* output);
// IA32 cdecl: load both input words with integer moves. In particular no x87
// input conversion can quiet a signaling NaN before the target receives it.
asm(".section .text.native_math_call_bits,\"ax\"\n.global NativeMathCallBits\n"
    "NativeMathCallBits:\npushl %ebp\nmovl %esp,%ebp\nandl $-16,%esp\nsubl $16,%esp\n"
    "movl 12(%ebp),%eax\nmovl (%eax),%ecx\nmovl %ecx,(%esp)\n"
    "movl 4(%eax),%ecx\nmovl %ecx,4(%esp)\ncall *8(%ebp)\n"
    "movl 16(%ebp),%eax\nfstpl (%eax)\nmovl %ebp,%esp\npopl %ebp\nret\n.text\n");
double (*volatile NativeMathFunctions[3])(double)={&log,&exp,&round};
void NativeMathChecks() {
    static_assert(nativeMathCaseCount==165,"frozen independent case count");
    static_assert(math_errhandling==(MATH_ERRNO|MATH_ERREXCEPT),"native public error contract");
    const int saved=errno,marker=17000+OS::GetCurrentThreadId();
    const unsigned cw=ControlWord(),mx=Mxcsr();
    unsigned environment[7];asm volatile("fnstenv %0; fldenv %0":"=m"(environment)::"memory");
    for (unsigned step=0;step<4;step++) {
        const unsigned mode=(step+OS::GetCurrentThreadId())&3U;
        SetControls((cw&~0xc00U)|(mode<<10),(mx&~0xe07fU)|(mode<<13));
        EnvironmentSanity(mode);
        const unsigned expectedCw=ControlWord(),expectedMx=Mxcsr()&~0x3fU;
        NativeMathRegression();
        for (unsigned seed=0;seed<2;seed++) {
            unsigned index=0;
            for (const auto& row:nativeMathCases) {
                NEED(row.function<3);
                native_tls_record.math_mode=mode;native_tls_record.math_seed=seed;
                native_tls_record.math_index=((row.function+1)<<24)|index;
                native_tls_record.math_input_lo=(unsigned)row.input;native_tls_record.math_input_hi=(unsigned)(row.input>>32);
                const unsigned long long absolute=row.input&0x7fffffffffffffffULL;
                const bool nan=absolute>0x7ff0000000000000ULL;
                const bool signaling=nan && !(absolute&0x8000000000000ULL);
                unsigned required=signaling?1U:0U;
                int expectedErrno=marker;
                if (row.function==0 && !nan) {
                    if (!absolute) { expectedErrno=ERANGE;required=4; }
                    else if (row.input>>63) { expectedErrno=EDOM;required=1; }
                }
                // C requires errno for overflow in the default rounding mode
                // and FE_OVERFLOW in every rounding mode when MATH_ERREXCEPT
                // is declared. Underflow reporting is implementation-defined.
                const bool expRange=row.function==1 && absolute<0x7ff0000000000000ULL
                    && (row.expected[mode]<0x10000000000000ULL || row.expected[mode]>=0x7fefffffffffffffULL);
                if (expRange && row.expected[0]==0x7ff0000000000000ULL) {
                    if (mode==0) expectedErrno=ERANGE;
                    required=8;
                }
                SetFlags(seed);errno=marker;unsigned long long actual=0;
                NativeMathCallBits(NativeMathFunctions[row.function],&row.input,&actual);
                const unsigned afterCw=ControlWord(),afterSw=StatusWord(),afterMx=Mxcsr();
                const int observedErrno=errno;
                native_tls_record.math_actual_lo=(unsigned)actual;native_tls_record.math_actual_hi=(unsigned)(actual>>32);
                native_tls_record.math_expected_lo=(unsigned)row.expected[mode];native_tls_record.math_expected_hi=(unsigned)(row.expected[mode]>>32);
                native_tls_record.math_cw=afterCw;native_tls_record.math_sw=afterSw;native_tls_record.math_mx=afterMx;
                native_tls_record.math_errno=(unsigned)observedErrno;native_tls_record.math_required=required;
                NEED(row.nan ? (actual&0x7ff0000000000000ULL)==0x7ff0000000000000ULL && (actual&0xfffffffffffffULL)
                    : actual==row.expected[mode]);
                NEED(afterCw==expectedCw && (afterMx&~0x3fU)==expectedMx);
                NEED(((afterSw|afterMx)&required)==required);
                NEED(((afterSw|afterMx)&1U)==((seed|required)&1U));
                if (expRange && expectedErrno==marker) NEED(observedErrno==marker || observedErrno==ERANGE);
                else NEED(observedErrno==expectedErrno);
                if ((index&7U)==0) {
                    Yield();NEED(errno==observedErrno && ControlWord()==expectedCw && (Mxcsr()&~0x3fU)==expectedMx);
                    NEED((StatusWord()&0x3fU)==(afterSw&0x3fU) && (Mxcsr()&0x3fU)==(afterMx&0x3fU));
                }
                asm volatile("lock incl %0":"+m"(native_tls_record.math_calls)::"cc","memory");index++;
            }
        }
    }
    asm volatile("fldenv %0"::"m"(environment):"memory");SetControls(cw,mx);errno=saved;
}
#endif
