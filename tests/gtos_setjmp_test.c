#include <setjmp.h>

extern int check_saved_registers(void);

static int check_result(int value) {
    jmp_buf saved;
    switch (setjmp(saved)) {
    case 0:
        longjmp(saved, value);
    case 1:
        return value != 0;
    case -23:
        return value != -23;
    default:
        return 1;
    }
}

static int check_nested(void) {
    jmp_buf outer, inner;
    switch (setjmp(outer)) {
    case 0:
        switch (setjmp(inner)) {
        case 0:
            longjmp(inner, 17);
        case 17:
            longjmp(outer, -3);
        default:
            return 1;
        }
    case -3:
        return 0;
    default:
        return 1;
    }
}

int gtos_setjmp_checks(void) {
    return check_result(0) | (check_result(-23) << 1) |
           (check_nested() << 2) | (check_saved_registers() << 3);
}
