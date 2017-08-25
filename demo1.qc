/* doc: NAMESPACE -*- C -*-
Demo file for testing out QC stuff
*/

static int magic = 0;

/* doc: Get the fibonacci number */
static int fib(int n)
{
        int i;
        int j;
        i = n + n;
        while (n < 300) {
                printf("%d\n", n);
                j = i;
                i = n + i;
                n = j;
        }
        return i;
}

/* doc: Test arrays of pointers */
void arrptr(void)
{
        int a, b, c, d, e;
        int *p[5];
        int i;
        int x;

        a = 13;
        b = 24;
        c = 35;
        d = 46;
        e = 57;
        p[0] = &a;
        p[1] = &b;
        p[2] = &c;
        p[3] = &d;
        p[4] = &e;
        i = 1;

        printf("%d should equal %d minus one\n", *p[i], *p[i] + 1);
        x = *(p[i] + 1);
        printf("%d should not equal %d\n", x, *p[i] + 1);
        printf("%d should equal %d (twenty-five)\n", *(p[i]) + 1, (*p[i]) + 1);

        for (i = 0; i < 7; ++i) {
                if (i == 5) {
                        if (i < 7)
                            break;
                }
                printf("*array[%d]=%d\n", i, *p[i] * 7);
        }
        printf("i=%d\n", i);
}

/* doc:
Test arrays of pointers. This is the entry
point for demo1.qc.
*/
void main(void)
{
        printf("Press a key to continue\n");
        getchar();

        arrptr();

        printf("MAGIC=0x%X\n", magic);
        fib(1);
        puts("done");
}

static void __init__(void)
{
        magic = 0xDEADBEEF;
}
