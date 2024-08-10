
#include <unistd.h>
#include <cstring>

#include <cassert>
#include <iostream>

#define MAX_SIZE 100000000

struct MallocMetadata
{
    size_t data_size;
    size_t block_size; //data_size + sizeof(metadata)
    bool is_free;
    MallocMetadata *next;
    MallocMetadata *prev;

    static void metadata_init(MallocMetadata* metadata, size_t data_size)
    {
        metadata->data_size = data_size;
        metadata->block_size = data_size + sizeof(MallocMetadata);
        metadata->is_free = false;
        metadata->next = NULL;
        metadata->prev = NULL;
    }
};

struct BlockManager{ //could do free list but no optimizations needed..
    MallocMetadata *head;
    MallocMetadata *tail;
    size_t num_free_blocks;
    size_t num_free_bytes;
    size_t num_allocated_blocks;
    size_t num_allocated_bytes;
    size_t num_meta_data_bytes;
    size_t size_meta_data;


    public:
        BlockManager() : head(NULL), tail(NULL), num_free_blocks(0), num_free_bytes(0), num_allocated_blocks(0), num_allocated_bytes(0), num_meta_data_bytes(0), size_meta_data(sizeof(MallocMetadata)) {};

        // return NULL if didn't find
        MallocMetadata *find_free_block(size_t size)
        {
            MallocMetadata *curr = this->head;
            while (curr != NULL)
            {
                if (curr->is_free == true && size <= curr->data_size)
                    break;
                curr = curr->next;
            }
            return curr;
        }

        void add(MallocMetadata *metadata)
        {
            if (metadata == NULL)
                return;

            if (this->head == NULL)
                this->head = metadata;
            if (this->tail != NULL)
            {
                this->tail->next = metadata;
                metadata->prev = this->tail;
            }
            this->tail = metadata;
            ++(this->num_allocated_blocks);
            this->num_allocated_bytes += metadata->data_size;
            this->num_meta_data_bytes += this->size_meta_data;
        }

        void mark_free(MallocMetadata* metadata)
        {
            metadata->is_free = true;
            ++(this->num_free_blocks);
            this-> num_free_bytes += metadata->data_size;
        }

        void mark_alloc(MallocMetadata* metadata)
        {
            metadata->is_free = false;
            --(this->num_free_blocks);
            this-> num_free_bytes -= metadata->data_size;
        }
};

BlockManager manager = BlockManager();



void* smalloc(size_t size)
{
    if (size == 0 || size > MAX_SIZE)
        return NULL;
    
    MallocMetadata* metadata;
    void *base_addr, *data_addr ;

    metadata = manager.find_free_block(size);
    if (metadata != NULL) // found free block
    {
        manager.mark_alloc(metadata);
        data_addr = (char*)metadata + sizeof(MallocMetadata);
        return data_addr;
    } else 
    {
        base_addr = sbrk(sizeof(MallocMetadata) + size);
        if (base_addr == (void *)(-1))
            return NULL;

        metadata = (MallocMetadata *)base_addr;
        data_addr = (char*)base_addr + sizeof(MallocMetadata);

        MallocMetadata::metadata_init(metadata, size);
        manager.add(metadata);
        return data_addr;
    }
  
}

void* scalloc(size_t num, size_t size)
{
    void* data_addr = smalloc(size*num);
    if (data_addr == NULL)
        return NULL;
    
    std::memset(data_addr, 0, size*num);
    return data_addr;
}

void sfree(void* p)
{
    if (p == NULL)
        return;

    void* metadata_addr = (char*)p - sizeof(MallocMetadata);
    MallocMetadata* metadata = (MallocMetadata*)metadata_addr;

    if (metadata->is_free)
        return;

    manager.mark_free(metadata);
}

void* srealloc(void* oldp, size_t size)
{
    if (size == 0 || size > MAX_SIZE)
        return NULL;
        
    if (oldp == NULL)
        return smalloc(size);

    void* old_metadata_addr = (char*)oldp - sizeof(MallocMetadata);
    MallocMetadata* old_metadata = (MallocMetadata*)old_metadata_addr;

    if (size <= old_metadata->data_size)
    {
        return oldp;
    }

    MallocMetadata* found_metadata = manager.find_free_block(size);
    void* newp;

    if (found_metadata == NULL) //didnt find existing block
    {
        newp = smalloc(size);
        if (newp == NULL)
            return NULL;

        std::memmove(newp, oldp, old_metadata->data_size);
        manager.mark_free(old_metadata);
        return newp;
    } else // found existing block
    {
        newp = (char*)found_metadata + sizeof(MallocMetadata);
        std::memmove(newp, oldp, old_metadata->data_size);

        manager.mark_alloc(found_metadata);
        manager.mark_free(old_metadata);
        
        return newp;
    }

}

size_t _num_free_blocks()
{
    return manager.num_free_blocks;
}

size_t _num_free_bytes()
{
    return manager.num_free_bytes;
}

size_t _num_allocated_blocks()
{
    return manager.num_allocated_blocks;
}

size_t _num_allocated_bytes()
{
    return manager.num_allocated_bytes;
}

size_t _num_meta_data_bytes()
{
    return manager.num_meta_data_bytes;
}

size_t _size_meta_data()
{
    return manager.size_meta_data;
}

// int main() {
//     // Test 1: Simple malloc and check if the entire block is marked as allocated
//     void* p1 = smalloc(100);
//     assert(p1 != nullptr);  // Ensure memory was allocated
//     assert(_num_allocated_blocks() == 1);
//     assert(_num_allocated_bytes() == 100);
//     assert(_num_meta_data_bytes() == _size_meta_data());
//     assert(_num_free_blocks() == 0);
//     assert(_num_free_bytes() == 0);  // All bytes in the block are used

//     // Test 2: Free the memory and check the free list
//     sfree(p1);
//     assert(_num_free_blocks() == 1);
//     assert(_num_free_bytes() == 100);
//     assert(_num_allocated_blocks() == 1);
//     assert(_num_allocated_bytes() == 100);
//     assert(_num_meta_data_bytes() == _size_meta_data());

//     // Test 3: Realloc to a larger size and check that the entire block is marked as free/allocated
//     void* p2 = smalloc(100);  // Allocate 100 bytes again
//     void* p3 = srealloc(p2, 200);  // Reallocate to 200 bytes
//     assert(p3 != nullptr);  // Ensure memory was reallocated
//     assert(_num_allocated_blocks() == 2);  // A new block was allocated
//     assert(_num_allocated_bytes() == 300);  // 100 (first block) + 200 (second block)
//     assert(_num_free_blocks() == 1);  // Old block is now free
//     assert(_num_free_bytes() == 100);  // The original 100-byte block is free
//     assert(_num_meta_data_bytes() == 2 * _size_meta_data());  // Two metadata blocks

//     // Test 4: Realloc to a smaller size and check that the entire block is still considered allocated
//     void* p4 = srealloc(p3, 100);  // Reallocate back to 100 bytes
//     assert(p4 == p3);  // Realloc should reuse the same block if shrinking
//     assert(_num_allocated_blocks() == 2);  // No new block allocated
//     assert(_num_allocated_bytes() == 300);  // Still considering the original allocations
//     assert(_num_free_blocks() == 1);  // One block is free
//     assert(_num_free_bytes() == 100);  // The original 100-byte block is free
//     assert(_num_meta_data_bytes() == 2 * _size_meta_data());  // Two metadata blocks

//     // Test 5: Calloc should reuse the 100-byte free block
//     size_t num = 5, size = 20;  // Request 100 bytes (5 * 20)
//     void* p5 = scalloc(num, size);
//     assert(p5 != nullptr);  // Ensure memory was allocated
//     assert(_num_allocated_blocks() == 2);  // No new block should be allocated
//     assert(_num_allocated_bytes() == 300);  // Allocated blocks remain the same
//     assert(_num_meta_data_bytes() == 2 * _size_meta_data());  // Still two metadata blocks
//     assert(_num_free_blocks() == 0);  // No free blocks remain
//     assert(_num_free_bytes() == 0);  // All blocks are in use

//     // Verify that the allocated memory is zero-initialized
//     char* p5_char = (char*)p5;
//     for (size_t i = 0; i < num * size; ++i) {
//         assert(p5_char[i] == 0);
//     }

//     // Test 6: Large allocation beyond limit
//     void* p6 = smalloc(100000001);  // Larger than allowed size
//     assert(p6 == nullptr);  // Should return NULL
    
//     assert(_num_allocated_blocks() == 2);
//     assert(_num_allocated_bytes() == 300);  // All previously allocated blocks
//     assert(_num_meta_data_bytes() == 2 * _size_meta_data());
//     assert(_num_free_blocks() == 0);
//     assert(_num_free_bytes() == 0);  // No free memory

//     // Test 7: Multiple allocations and free
//     void* p7 = smalloc(150);
//     void* p8 = smalloc(250);
//     assert(p7 != nullptr);
//     assert(p8 != nullptr);
//     assert(_num_allocated_blocks() == 4);  // Additional two blocks
//     assert(_num_allocated_bytes() == 700);  // 300 + 150 + 250
//     assert(_num_meta_data_bytes() == 4 * _size_meta_data());

//     sfree(p8);
//     assert(_num_free_blocks() == 1);  // Now 1 free block
//     assert(_num_free_bytes() == 250);  // The 250-byte block is free

//     std::cout << "All tests passed!" << std::endl;
//     return 0;
// }
