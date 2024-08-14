
#include <unistd.h>
#include <cstring>
#include <cstdint>
#include <sys/mman.h>

#include <iostream>
#include <cassert>

#define MAX_SIZE 100000000
#define MAX_ORDER 10
#define MIN_BLOCK_SIZE 128
#define MAX_BLOCK_SIZE (MIN_BLOCK_SIZE << MAX_ORDER)
#define TOT_BLOCKS_SIZE (32*MAX_BLOCK_SIZE)

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
        metadata->is_free = true;
        metadata->next = NULL;
        metadata->prev = NULL;
    }

    static void metadata_init_block(MallocMetadata* metadata, size_t block_size)
    {
        metadata->block_size = block_size;
        metadata->data_size = block_size - sizeof(MallocMetadata);
        metadata->is_free = true;
        metadata->next = NULL;
        metadata->prev = NULL;
    }
};

struct LevelManager{
    MallocMetadata *head;
};

struct BlockManager{ 
    LevelManager level_manager[MAX_ORDER + 2];
    size_t num_free_blocks;
    size_t num_free_bytes;
    size_t num_allocated_blocks;
    size_t num_allocated_bytes;
    size_t num_meta_data_bytes;
    size_t size_meta_data;

    BlockManager() : num_free_blocks(0), num_free_bytes(0), num_allocated_blocks(0), num_allocated_bytes(0), num_meta_data_bytes(0), size_meta_data(sizeof(MallocMetadata)) {
        for (int i = 0; i < MAX_ORDER + 2; i++)
        {
            level_manager[i].head = NULL;
        }
        
    };

    void init()
    {
        void *current_brk;
        size_t to_align;
        MallocMetadata* metadata;


        current_brk = sbrk(0); // Get current break

        to_align = TOT_BLOCKS_SIZE - (uintptr_t)current_brk%TOT_BLOCKS_SIZE;

        if (to_align != 0)
            sbrk(to_align);

        current_brk = sbrk(TOT_BLOCKS_SIZE);
        to_align = (uintptr_t)current_brk % TOT_BLOCKS_SIZE; //just to see its 0

        for (size_t i = 0; i < 32; i++)
        {
            metadata = (MallocMetadata*)current_brk;
            MallocMetadata::metadata_init_block(metadata, MAX_BLOCK_SIZE);
            add_new_block(metadata);

            current_brk = (char*)current_brk + MAX_BLOCK_SIZE;
        }
    }

    // return NULL if didn't find
    MallocMetadata *find_free_block(size_t size)
    {
        MallocMetadata* found_block;
        size_t lvl = _calc_lvl(size);
        size_t found_lvl = lvl + 1;
        MallocMetadata* tight_block;

        if (lvl > MAX_ORDER) // TODO change later
            return NULL;
        
        found_block = level_manager[lvl].head;
        if (found_block != NULL)
            return found_block;
        

        while (found_lvl <= MAX_ORDER)
        {
            found_block = level_manager[found_lvl].head;
            if (found_block != NULL)
                break;

            ++found_lvl;
        }
        if (found_block == NULL)
            return NULL;
        
        for (size_t i = lvl; i < found_lvl; i++)
        {
            tight_block = split_block(found_block);
        }
        

        return tight_block;
    }

    void add_new_block(MallocMetadata *metadata)
    {
        if (metadata == NULL)
            return;


        if (metadata->is_free)
            _insert(metadata);
        _data_add_block(metadata);
    }

    void delete_block(MallocMetadata *metadata)
    {
        if (metadata == NULL)
            return;
        
        if (metadata->is_free)
            _remove(metadata);
        _data_remove_block(metadata);
    }

    void mark_free_bin_block(MallocMetadata *metadata)
    {
        metadata->is_free = true;
        _insert(metadata);
        ++num_free_blocks;
        num_free_bytes += metadata->data_size;
    }

    void mark_alloc_bin_block(MallocMetadata *metadata)
    {
        metadata->is_free = false;
        _remove(metadata);
        --num_free_blocks;
        num_free_bytes -= metadata->data_size;
    }

    MallocMetadata* split_block(MallocMetadata* metadata)
    {
        MallocMetadata* buddy_metadata;
        bool is_free = metadata->is_free;
        size_t block_size = metadata->block_size;
        size_t new_block_size = block_size >> 1;
        size_t lvl = _calc_lvl(block_size);

        if (lvl == 0)
            return NULL;

        delete_block(metadata);

        MallocMetadata::metadata_init_block(metadata, new_block_size);
        buddy_metadata  = _get_buddy(metadata);
        MallocMetadata::metadata_init_block(buddy_metadata,new_block_size);

        if (is_free == false)
            metadata->is_free = false;
        
        add_new_block(metadata);
        add_new_block(buddy_metadata);

        return metadata;
    }


    MallocMetadata* join_block_to_buddy(MallocMetadata* metadata)
    {
        MallocMetadata* buddy_metadata;
        MallocMetadata* new_metadata;

        bool is_free = metadata->is_free;
        size_t block_size = metadata->block_size;
        size_t new_block_size = metadata->block_size << 1;
        size_t lvl = _calc_lvl(block_size);

        if (lvl >= MAX_ORDER)
            return NULL;

        buddy_metadata = _get_buddy(metadata);
        new_metadata = buddy_metadata > metadata ? metadata : buddy_metadata;

        if (_check_if_free(buddy_metadata, block_size) == false)
            return NULL;

        delete_block(metadata);
        delete_block(buddy_metadata);

        MallocMetadata::metadata_init_block(new_metadata, new_block_size);

        if (is_free == false)
            new_metadata->is_free = false;

        add_new_block(new_metadata);
        
        return new_metadata;
    }

    size_t check_max_block_size_after_joins(MallocMetadata* metadata)
    {
        MallocMetadata* buddy_metadata;
        MallocMetadata* new_metadata = metadata;

        size_t new_block_size = metadata->block_size;

        while (true)
        {
            size_t lvl = _calc_lvl(new_block_size);
            buddy_metadata = _do_get_buddy(new_metadata, new_block_size);
        if (lvl >= MAX_ORDER || (_check_if_free(buddy_metadata,new_block_size ) == false)) //can't join
        {
            return new_block_size;
        }

            new_metadata = buddy_metadata > new_metadata ? new_metadata : buddy_metadata;
            new_block_size <<= 1;
        }
    }


    private:

    MallocMetadata* _do_get_buddy(void* addr, size_t block_size)
    {
        uintptr_t buddy_addr = ((uintptr_t)addr) ^ (block_size); // TODO check that the conversion is valid

        return (MallocMetadata *)buddy_addr;
    }
    MallocMetadata* _get_buddy(MallocMetadata* metadata)
    {
        size_t block_size = metadata->block_size;
        return _do_get_buddy(metadata,block_size);
    }


    size_t _calc_lvl(int size)
    {
        int result = 0;
        int temp = size;
        while (temp >>= 1)
            result++;

        if ((1 << result) < size)
            ++result;
        
        result = result - 7;
        if (result < 0)
             return 0;
        if (result > MAX_ORDER)
            return 11;

        return result;
    }

    void _insert(MallocMetadata* metadata)
    {
        size_t lvl = _calc_lvl(metadata->block_size);
        MallocMetadata *lvl_head = this->level_manager[lvl].head;
        if (lvl_head == NULL)
        {
            this->level_manager[lvl].head = metadata;
        }else if (metadata < lvl_head) //replace head
        {
            lvl_head->prev = metadata;
            metadata->next = lvl_head;
            this->level_manager[lvl].head = metadata;
        } else //search for suitble place
        {
            MallocMetadata* iter = lvl_head;
            while (iter != NULL)
            {
                if ((metadata > iter && iter->next == NULL) || (metadata > iter && metadata < iter->next)){
                    metadata->next = iter->next;
                    metadata->prev = iter;
                    iter->next = metadata;
                    if (metadata->next != NULL)
                        metadata->next->prev = metadata;
                }
                iter = iter ->next;
            }
        }
    }

    void _remove(MallocMetadata* metadata)
    {
        size_t lvl = _calc_lvl(metadata->block_size);
        MallocMetadata *prev, *next;
        prev = metadata->prev;
        next = metadata->next;
        
        if (prev == NULL && next == NULL){
            this->level_manager[lvl].head = NULL;
        } else if (prev == NULL && next != NULL){
            this->level_manager[lvl].head = next;
            next->prev = NULL;
        } else if (prev != NULL && next == NULL){
            prev->next = NULL;
        } else if (prev != NULL && next != NULL){
            prev->next = next;
            next->prev = prev;
        }

        metadata->prev = metadata->next = NULL; // unnaccesary but feels right
    }

    void _data_add_block(MallocMetadata* metadata)
    {
        if (metadata->is_free){
            ++num_free_blocks;
            num_free_bytes += metadata->data_size;
        }
        ++num_allocated_blocks;
        num_allocated_bytes += metadata->data_size;
        num_meta_data_bytes += sizeof(MallocMetadata);
    }
    void _data_remove_block(MallocMetadata* metadata)
    {
        if (metadata->is_free){
            --num_free_blocks;
            num_free_bytes -= metadata->data_size;
        }
        --num_allocated_blocks;
        num_allocated_bytes -= metadata->data_size;
        num_meta_data_bytes -= sizeof(MallocMetadata);
    }

    bool _check_if_free(MallocMetadata* block, size_t expected_block_size)
    {
        if(block->block_size == expected_block_size){
            return block->is_free;
        }else{ // can only be lower than expected..
            size_t new_expected_size = expected_block_size >> 1;
            MallocMetadata* buddy = _do_get_buddy(block,new_expected_size);
            return _check_if_free(block, new_expected_size) && _check_if_free(buddy,new_expected_size);
        }
        

    }


};

BlockManager manager = BlockManager();



void* smalloc(size_t size)
{
    static bool to_alloc = true;
    if (to_alloc){
        manager.init();
        to_alloc = false;
    }
    
    if (size == 0 || size > MAX_SIZE)
        return NULL;
    
    MallocMetadata* metadata;
    size_t needed_size = size + sizeof(MallocMetadata);
    void *metadata_addr, *data_addr ;

    if (needed_size > MAX_BLOCK_SIZE) // handle with mmap
    {
        metadata_addr = mmap(NULL, needed_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (metadata_addr == MAP_FAILED)
            return NULL;

        metadata = (MallocMetadata*)metadata_addr;
        MallocMetadata::metadata_init_block(metadata, needed_size);
        metadata->is_free = false;
        manager.add_new_block(metadata);

        data_addr = (char*)metadata_addr + sizeof(MallocMetadata);
        return data_addr;
    }

    metadata = manager.find_free_block(needed_size);
    if (metadata == NULL)
        return NULL;
    

    manager.mark_alloc_bin_block(metadata);
    
    data_addr = (char*)metadata + sizeof(MallocMetadata);
    return data_addr;
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

    if (metadata->block_size > MAX_BLOCK_SIZE) // handle with mmap
    {
        manager.delete_block(metadata);
        munmap(metadata, metadata->block_size);

        return;
    }
    manager.mark_free_bin_block(metadata);
    while ((metadata = manager.join_block_to_buddy(metadata)) != NULL);
    
}

void* srealloc(void* oldp, size_t size)
{
    if (size == 0 || size > MAX_SIZE)
        return NULL;
        
    if (oldp == NULL)
        return smalloc(size);

    void* newp;
    void* old_metadata_addr = (char*)oldp - sizeof(MallocMetadata);
    size_t needed_size = size + sizeof(MallocMetadata);
    MallocMetadata* old_metadata = (MallocMetadata*)old_metadata_addr;
    MallocMetadata* new_metadata;
    size_t new_block_size;


    if (needed_size > MAX_BLOCK_SIZE) // handle with mmap
    {
        if(old_metadata->block_size == needed_size)
            return oldp;
            
        newp = smalloc(size);
        std::memmove(newp, oldp, old_metadata->data_size);
        sfree(oldp);
        return newp;
    }

    new_metadata = old_metadata; // not neccessary
    MallocMetadata *iter = old_metadata;
    if (needed_size <= old_metadata->block_size)
    {
        // while (iter != NULL)
        // {
        //     new_metadata = iter;
        //     if ((iter->block_size >> 1) < needed_size)
        //         break;
        //     iter = manager.split_block(iter);
        // }

        // newp = (char *)new_metadata + sizeof(MallocMetadata);
        // return newp;
        return oldp;

    } else // needed_size < old_metadata->block_size
    {
        if ((new_block_size = manager.check_max_block_size_after_joins(old_metadata)) >= needed_size) // the current block after joins can accomodate
        {
            while (iter != NULL && needed_size > iter->block_size)
            {
                new_metadata = iter;
                iter = manager.join_block_to_buddy(new_metadata);
            }

            new_metadata = iter == NULL ? new_metadata : iter;
            newp = (char *)new_metadata + sizeof(MallocMetadata);
            std::memmove(newp, oldp, old_metadata->data_size);
            return newp;
        }
        else // gets new bin block
        {
            newp = smalloc(size);
            std::memmove(newp, oldp, old_metadata->data_size);
            sfree(oldp);
            return newp;
        }
    }

    return NULL;
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
//     // Test 1: Allocate and Free a Small Block (140 bytes)
//     void* ptr1 = smalloc(100);  // Needs 140 bytes total (100 + 40 metadata)
//     void* ptr20 = srealloc(ptr1,250);
//     assert(ptr1 != nullptr);
//     assert(_num_allocated_blocks() == 41); // 41 blocks allocated
//     assert(_num_free_blocks() == 40); // 40 blocks free

//     sfree(ptr1);
//     assert(_num_allocated_blocks() == 32);  // Back to 32 blocks after merging
//     assert(_num_free_blocks() == 32);  // All blocks are free again

//     // Test 2: Allocate and Free a Block Exactly Matching 128KB + Metadata
//     void* ptr2 = smalloc(128 * 1024); // Requires 131,112 bytes (128KB + 40B)
//     assert(ptr2 != nullptr);
//     assert(_num_allocated_blocks() == 33);  // 32 original blocks + 1 large block allocated (256KB)
//     assert(_num_free_blocks() == 32); // 31 free blocks remain after the allocation

//     sfree(ptr2);
//     assert(_num_allocated_blocks() == 32);  // Back to 32 blocks after freeing and merging
//     assert(_num_free_blocks() == 32);  // All blocks are free again

//     // Test 3: Allocate a Block, Then Allocate Another One That Forces Splitting
//     void *ptr3 = smalloc(128 * 1024); // Requires 131,112 bytes
//     assert(ptr3 != nullptr);
//     assert(_num_allocated_blocks() == 33); // 1 block of 256KB allocated, 32 blocks remain

//     void *ptr4 = smalloc(100); // Requires 140 bytes total
//     assert(ptr4 != nullptr);
//     assert(_num_allocated_blocks() == 42); // 33 from before + 9 new blocks due to splitting down for 140 bytes
//     assert(_num_free_blocks() == 40);      // 40 blocks are free

//     sfree(ptr3);
//     sfree(ptr4);
//     assert(_num_allocated_blocks() == 32); // Back to 32 blocks after merging
//     assert(_num_free_blocks() == 32);      // All blocks are free again

//     // Test 4: Allocate All Available Memory, Then Free
//     void *allocations[32];
//     for (int i = 0; i < 32; i++)
//     {
//         allocations[i] = smalloc(128 * 1024 - 40); // Allocating close to 128KB each, accounting for metadata
//         assert(allocations[i] != nullptr);
//     }
//     assert(_num_allocated_blocks() == 32); // All blocks are now allocated
//     assert(_num_free_blocks() == 0);       // No free blocks remain

//     for (int i = 0; i < 32; i++)
//     {
//         sfree(allocations[i]);
//     }
//     assert(_num_allocated_blocks() == 32); // After freeing, back to 32 blocks
//     assert(_num_free_blocks() == 32);      // All blocks are free again

//     // Test 5: Allocate and Free Large Blocks (Beyond Buddy System, Using mmap)
//     void *ptr5 = smalloc(MAX_BLOCK_SIZE + 1); // Requires mmap
//     assert(ptr5 != nullptr);
//     assert(_num_allocated_blocks() == 33); // One block via mmap, others unchanged
//     assert(_num_free_blocks() == 32);      // No internal blocks affected

//     sfree(ptr5);
//     assert(_num_allocated_blocks() == 32); // Back to 32 after freeing mmap block
//     assert(_num_free_blocks() == 32);      // All blocks are free again

//     // Test 6: Reallocate, Triggering Merging of Used Blocks
//     void *ptr6 = smalloc(100);        // Needs 140 bytes total (100 + 40 metadata)
//     void *ptr7 = srealloc(ptr6, 400); // Should cause merging of 256-byte blocks into 512-byte block
//     assert(ptr7 != nullptr);

//     assert(_num_allocated_blocks() == 40); // Merging occurred, reducing the number of blocks
//     assert(_num_free_blocks() == 39);      // 39 blocks free, one used for 512-byte block

//     sfree(ptr7);
//     assert(_num_allocated_blocks() == 32); // Back to 32 blocks after freeing and merging
//     assert(_num_free_blocks() == 32);      // All blocks are free again

//     // Test 7: Edge Case - Allocate Smallest Possible Block (Including Metadata)
//     void *ptr8 = smalloc(1); // Allocating 1 byte + 40 bytes metadata, total 41 bytes
//     assert(ptr8 != nullptr);
//     assert(_num_allocated_blocks() == 42); // Splitting down to a 128-byte block results in 42 allocated blocks
//     assert(_num_free_blocks() == 41);      // 41 blocks remain free after using one 128-byte block

//     sfree(ptr8);
//     assert(_num_allocated_blocks() == 32); // Back to 32 blocks after freeing and merging
//     assert(_num_free_blocks() == 32);      // All blocks are free again

//     // Test 8: Allocate and Free Multiple Blocks in Different Orders
//     void *ptr9 = smalloc(50);   // Needs 90 bytes total (50 + 40 metadata)
//     void *ptr10 = smalloc(150); // Needs 190 bytes total (150 + 40 metadata)
//     void *ptr11 = smalloc(100); // Needs 140 bytes total (100 + 40 metadata)

//     assert(ptr9 != nullptr && ptr10 != nullptr && ptr11 != nullptr);
//     assert(_num_allocated_blocks() == 43); // 43 blocks allocated due to previous allocations and splits
//     assert(_num_free_blocks() == 40);      // 40 blocks remain free after using one 256-byte block

//     sfree(ptr10);
//     sfree(ptr9);
//     sfree(ptr11);
//     assert(_num_allocated_blocks() == 32); // Back to 32 blocks after freeing and merging
//     assert(_num_free_blocks() == 32);      // All blocks are free again


//     std::cout << "All tests passed!" << std::endl;
//     return 0;
// }
