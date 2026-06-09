#include <stdio.h>

#if defined(_MSC_VER) && (_MSC_VER >= 1900)

extern "C"
{
    FILE * __cdecl __iob_func(void)
    {
        static FILE iob[] =
        {
            *stdin,
            *stdout,
            *stderr
        };

        return iob;
    }
}

#endif
