int main(int argc, char *argv[]);
void proc_0x08049e90();
void proc_0x08049ac0(__size32 param1, __size32 param2, __size32 param3, __size32 param4);
void proc_0x08048a30(int param1);
void proc_0x080498b0(union { FILE *; __size32; } param1, union { char[] *; __size32; } param2, union { char[] *; __size32; } param3, union { char[] *; __size32; } param4, union { __size32 *; __size32; } param5);

/** address: 0x08048b10 */
int main(int argc, char *argv[])
{
    char *eax; 		// r24
    int ecx; 		// r25
    __size32 edi; 		// r31
    __size32 esi; 		// r30

    eax = *argv;
    *(char *[]*)0x804b928 = eax;
    setlocale(6, "");
    bindtextdomain("coreutils", "/usr/share/locale");
    textdomain("coreutils");
    proc_0x08049e90();
    if (argc == 2) {
        eax = getenv("POSIXLY_CORRECT");
        flags = LOGICALFLAGS32(eax);
        if (eax == 0) {
            eax = *(argv + 4);
            edi = 0x804a055;
            esi = eax;
            ecx = 7;
            do {
                tmpb = *esi - *edi;
                flags = SUBFLAGS8(*esi, *edi, tmpb);
                esi +=  (DF == 0) ? 1 : -1;
                edi +=  (DF == 0) ? 1 : -1;
                ecx = ecx - 1;
            } while (tmpb == 0);
            if (flags) {
                proc_0x08048a30(0);
            }
            if (r[25] != 0) {
                do {
                } while (ZF);
            }
            if ( ~flags) {
                *(__size32*)(r[28] + 20) = r[24];
                *(__size32*)(r[28] + 16) = r[31];
                *(__size32*)(r[28] + 12) = r[30];
                *(__size32*)(r[28] + 8) = r[27];
                *(__size32*)(r[28] + 4) = r[25];
                *(__size32*)r[28] = r[24];
                proc_0x08049ac0(*(r[28] + 4), *(r[28] + 8), *(r[28] + 12), *(r[28] + 16));
            }
        }
    }
    *(__size32*)r[28] = 0;
    exit(*(r[28] + 4));
    return;
}

/** address: 0x08049e90 */
void proc_0x08049e90()
{
    __size32 eax; 		// r24

    if (eax != 0) {
    }
    __cxa_atexit();
    return;
}

/** address: 0x08049ac0 */
void proc_0x08049ac0(__size32 param1, __size32 param2, __size32 param3, __size32 param4)
{
    __size32 esp; 		// r28

    proc_0x080498b0(param1, param2, param3, param4, (esp + 20));
    return;
}

/** address: 0x08048a30 */
void proc_0x08048a30(int param1)
{
    __size32 eax; 		// r24
    char *eax_1; 		// r24{0}
    int edx; 		// r26

    eax_1 = dcgettext(0, "Usage: %s [ignored command line arguments]\n  or:  %s OPTION\nExit with a status code indicating success.\n\nThese option names may not be abbreviated.\n\n", 5);
    printf(eax_1);
    eax = dcgettext(0, "      --help     display this help and exit\n", 5);
    edx = *0x804b7e0;
    fputs_unlocked(eax, edx);
    eax = dcgettext(0, "      --version  output version information and exit\n", 5);
    edx = *0x804b7e0;
    fputs_unlocked(eax, edx);
    eax = dcgettext(0, "\nReport bugs to <%s>.\n", 5);
    printf(eax);
    exit(param1);
    return;
}

/** address: 0x080498b0 */
void proc_0x080498b0(union { FILE *; __size32; } param1, union { char[] *; __size32; } param2, union { char[] *; __size32; } param3, union { char[] *; __size32; } param4, union { __size32 *; __size32; } param5)
{
    int eax; 		// r24
    int ebx_1; 		// r27{0}
    __size32 ebx_2; 		// r27{0}
    int ebx_5; 		// r27{0}
    int ebx_8; 		// r27{0}
    int ebx_9; 		// r27{0}
    int edx; 		// r26
    char *local1; 		// m[esp - 40]
    int local11; 		// ebx_8{0}
    int local12; 		// ebx_1{0}
    union { char *; int; } local13; 		// local1{0}
    int local5; 		// m[esp - 40]

    ebx_5 = 0;
    edx = param5 + 4;
    eax = *param5;
    local11 = ebx_5;
    local12 = ebx_5;
    if (eax != 0) {
        do {
            ebx_8 = local11;
            ebx_9 = ebx_8 + 1;
            eax = *edx;
            edx += 4;
            local11 = ebx_9;
            local12 = ebx_9;
        } while (eax != 0);
    }
    ebx_1 = local12;
    if (param2 == 0) {
        fprintf(param1, "%s %s\n", param3, param4);
    }
    else {
        fprintf(param1, "%s (%s) %s\n", param2, param3, param4);
    }
    if ((unsigned int)ebx_1 > (unsigned int)9) {
        eax = 0x804a574;
        local5 = eax;
        local13 = local5;
        local1 = local13;
        eax = dcgettext(0, local1, 5);
        vfprintf(param1, eax, param5);
        eax = *(param1 + 20);
        if (eax < *(param1 + 24)) {
            *(int*)eax = 10;
            *(__size32*)(param1 + 20)++;
            eax = *0x804b7d0;
            fputs_unlocked(eax, param1);
            eax = *(param1 + 20);
            if (eax < *(param1 + 24)) {
                *(int*)eax = 10;
                *(__size32*)(param1 + 20)++;
                eax = dcgettext(0, "This is free software; see the source for copying conditions.  There is NO\nwarranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n", 5);
                fputs_unlocked(eax, param1);
                return;
            }
            __overflow();
bb0x80499ce:
            goto bb0x80499ce;
        }
        __overflow();
bb0x80499e1:
        goto bb0x80499e1;
    }
    switch(ebx_2) {
    case 0:
    case 1:
bb0x804992c:
        eax = 0x804a5bc;
        goto bb0x804992c;
    case 2:
bb0x80499e3:
        local1 = 0x804a5cc;
        local13 = local1;
        goto bb0x80499e3;
    case 3:
bb0x80499fa:
        eax = 0x804a5e3;
        goto bb0x80499fa;
    case 4:
bb0x8049a0d:
        eax = 0x804a478;
        goto bb0x8049a0d;
    case 5:
bb0x8049a20:
        eax = 0x804a498;
        goto bb0x8049a20;
    case 6:
bb0x8049a33:
        local1 = 0x804a4bc;
        local13 = local1;
        goto bb0x8049a33;
    case 7:
bb0x8049a4a:
        local1 = 0x804a4e4;
        local13 = local1;
        goto bb0x8049a4a;
    case 8:
bb0x8049a61:
        eax = 0x804a510;
        goto bb0x8049a61;
    case 9:
bb0x8049a74:
        eax = 0x804a540;
        goto bb0x8049a74;
    }
    abort();
    return;
}

