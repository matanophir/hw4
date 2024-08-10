#include <unistd.h>


void* smalloc(size_t size)
{
    if (size == 0 || size > 100000000)
        return NULL;
    
    void* prev_addr = sbrk(size);
    // void* prev_addr = (void*)(-1);

    if (prev_addr == (void*)(-1))
        return NULL;
    
    return prev_addr;
}


// int main(int argc, char const *argv[])
// {
//     void* b_addr = smalloc(-1);

//     return 0;
// }
