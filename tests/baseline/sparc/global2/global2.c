int b = 7;
double a = 5.2;

void foo1();
void foo2();

// address: 10754
int main(int argc, char *argv[], char *envp[]) {
    foo1();
    printf("b = %i\n", b);
    return 0;
}

// address: 1073c
void foo1() {
    foo2();
    return;
}

// address: 106fc
void foo2() {
    b = 12;
    printf("a = %f\n", a);
    return;
}

