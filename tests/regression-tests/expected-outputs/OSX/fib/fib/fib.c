int main(int argc, char *argv[]);
__size32 fib(int param2, __size32 param3, __size32 param3);

/** address: 0x00001ce4 */
int main(int argc, char *argv[])
{
    __size32 g29; 		// r29
    int g9; 		// r9

    fib(10, g9, g29);
    printf(/* machine specific */ (int) LR + 768);
    return 0;
}

/** address: 0x00001d38 */
__size32 fib(int param2, __size32 param3, __size32 param3)
{
    __size32 g1; 		// r1
    __size32 g29_1; 		// r29{0}
    __size32 g29_2; 		// r29{0}
    __size32 g29_5; 		// r29{0}
    __size32 g29_6; 		// r29{0}
    int g3; 		// r3
    __size32 g30_1; 		// r30{0}
    __size32 g30_2; 		// r30{0}
    __size32 g30_5; 		// r30{0}
    __size32 g30_6; 		// r30{0}
    int g9; 		// r9
    __size32 local7; 		// param3{0}
    __size32 local8; 		// g29_5{0}
    __size32 local9; 		// g30_5{0}

    g30_1 = g1 - 96;
    local7 = param3;
    local8 = param3;
    local9 = g30_1;
    if (param2 > 1) {
        g3 = fib(param2 - 1, param2, param3);
        g3 = fib(g9 - 2, g9, g3); /* Warning: also results in g9, g29_2, g30_2 */
        local7 = g9;
        local9 = g30_2;
        g29_1 = g29_2 + g3;
        *(__size32*)(g30_2 + 64) = g29_2 + g3;
        local8 = g29_1;
    }
    else {
    }
    param3 = local7;
    g29_5 = local8;
    g30_5 = local9;
    g3 = *(g30_5 + 64);
    return g29_6; /* WARNING: Also returning: g30 := g30_6, g3 := g3, g9 := param3, g29 := g29_6, g29 := g29_6, g29 := g29_5, g30 := g30_6, g30 := g30_6, g30 := g30_5 */
}

