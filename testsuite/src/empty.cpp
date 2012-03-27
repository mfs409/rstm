#include <cstdio>

int main(int argc, char* const []) {
    char foo[] = "foo";
    __transaction_atomic {
        foo[0] = 'b';
        if (argc > 1)
            __transaction_cancel;
    }
    printf("%s\n", foo);
    return 0;
}
