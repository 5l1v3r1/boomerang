int main(int argc, char *argv[]);

/** address: 0x00010b0c */
int main(int argc, char *argv[])
{
    int i1; 		// r25
    int o0; 		// r8
    int o1; 		// r9
    int o2; 		// r10
    int o3; 		// r11

    o0 = 0x10a5c;
    o3 = 0x10a8c;
    o2 = 0x10abc;
    o1 = 0x10aec;
    if (o0 != 0x10a5c) {
        i1 = 0;
    }
    else {
bb0x10b9c:
        if (o3 != 0x10a8c || o2 != 0x10abc || o1 != 0x10aec) {
            goto bb0x10b9c;
        }
        else {
            i1 = 1;
        }
    }
    if (i1 == 0) {
        printf(0x10d00);
    }
    else {
        printf(0x10cf8);
    }
    return 0;
}

