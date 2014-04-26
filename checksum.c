/*
**  CHECKSUM.C - Compute the checksum of a file
**
**  public somain demo by Bob Stout 
**  Modified by Minix Monkeys
**   (╯°□°）╯︵ ┻━┻
*/
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* Adds up all the bytes in checksum */
unsigned checksum(void *buffer, size_t len, unsigned int seed, FILE *fpOut)
{
      unsigned char *buf = (unsigned char *)buffer;
      size_t i;
    
      for (i = 0; i < len; ++i){
        fprintf(fpOut,"%c",*buf);      
        seed += (unsigned int)(*buf++);
      }

      return seed;
}

int main(int argc, char** argv)
{
      FILE *fp;
      FILE *fpOut;
      size_t len;

      char buf[1024], *outFile = "newFile.c";

      assert(argc > 1);

      fpOut = fopen(outFile,"w+");

      /*This will open a new file and check for the bytes *
      * else will print out an error.len return number of *
      * bytes read and then prints out checksum           */

      if (NULL == (fp = fopen(argv[1], "r+")))
      {
         printf("Unable to open %s for reading\n", argv[1]);
         return -1;
      }

      while(1) {
        len = fread(buf, sizeof(char), sizeof(buf), fp);

        if(!len)
          break;

        printf("Checksum is %#x.\n", checksum(buf, len, 0, fpOut));
      }

      return EXIT_SUCCESS;
}
