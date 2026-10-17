#ifndef GTOS_NATIVE_MATH_REGRESSION_H
#define GTOS_NATIVE_MATH_REGRESSION_H
double (*volatile NativeMathUnaryRegression[2])(double)={&ceil,&floor};
double (*volatile NativeMathBinaryRegression[2])(double,double)={&fmod,&pow};
void NativeMathRegression() {
    const int marker=19000+OS::GetCurrentThreadId();errno=marker;
    const unsigned cw=ControlWord(),mx=Mxcsr()&~0x3fU;
    NEED(Bits(NativeMathUnaryRegression[0](-0.25))==0x8000000000000000ULL);
    NEED(Bits(NativeMathUnaryRegression[1](0.25))==0);
    NEED(Bits(NativeMathBinaryRegression[0](5.5,2.0))==0x3ff8000000000000ULL);
    NEED(Bits(NativeMathBinaryRegression[1](1.5,2.0))==0x4002000000000000ULL);
    NEED(errno==marker && ControlWord()==cw && (Mxcsr()&~0x3fU)==mx);
}
#endif
