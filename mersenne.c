/*@@   MERSENNE.C   @@*/
/*@@ SCOTT  COMPTON @@*/

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#define MTSIZE 624

uint32_t mtArr[MTSIZE];
unsigned mtIndex;

void mtGenerate(void)
{
    uint32_t val;
    unsigned i;

    for(i = 0; i < MTSIZE; i++) {
        val = (mtArr[i] & 0x80000000) + (mtArr[(i + 1) % MTSIZE] & 0x7FFFFFFF);
        mtArr[i] = (mtArr[(i + 397) % MTSIZE] ^ (val >> 1));

        if((val % 2) != 0)
            mtArr[i] = (mtArr[i] ^ 2567483615);
    }
}

uint32_t mtExtract(void)
{
    uint32_t val;

    if(mtIndex == 0)
        mtGenerate();

    val = mtArr[mtIndex];
    val = (val ^ (val >> 11));
    val = (val ^ ((val <<  7) & 2636928640));
    val = (val ^ ((val << 15) & 4022730752));
    val = (val ^ (val >> 18));

    mtIndex = ((mtIndex + 1) % 624);

    return val;
}

void mtInitialize(uint32_t seed)
{
    unsigned i;

    mtArr[0] = seed;
    mtIndex  = 0;

    for(i = 1; i < MTSIZE; i++)
        mtArr[i] = (i + (1812433253 * (mtArr[i - 1] ^ (mtArr[i - 1] >> 30))));
}

int main(int argc, char** argv)
{

    uint32_t val;
    unsigned count;

    srand(time(NULL));
    mtInitialize(rand());

    assert(argc > 1);

    count = atoi(argv[1]);

    while(count > 0) {
        val = mtExtract();
        count--;
    }

    return EXIT_SUCCESS;

}
