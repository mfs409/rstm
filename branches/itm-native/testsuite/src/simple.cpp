static int __attribute__((noinline)) foo(int* args) {
    __transaction_atomic {
        return (*args > 1) ? 1 : 0;
    }
}

int
main(int argc, char* const []) {
    return foo(&argc);
}
