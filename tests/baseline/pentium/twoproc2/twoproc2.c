// address: 0x8048333
int main(int argc, char *argv[], char *envp[]) {
    __size32 eax; 		// r24

    proc1();
    proc2();
    proc1();
    proc2();
    return eax;
}

