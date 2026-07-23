#include <windows.h>
#include <stdio.h>

int
main(void)
{
#ifndef _WIN64
        fprintf(stderr, "compiling in 32 bits when x64 was asked?!\n");
#endif
        return(0);
}
