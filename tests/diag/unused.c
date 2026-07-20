/* 'scratch' is never read. 'used' is. '_intentional' opts out by name. */
int main(void) {
    int used = 1;
    int scratch = 2;
    int _intentional = 3;
    return used;
}
