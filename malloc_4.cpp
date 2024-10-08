
#include <unistd.h>
#include <cstring>
#include <cstdint>
#include <sys/mman.h>
#include <fstream>
#include <string>

#include <iostream>
#include <cassert>

#define MAX_SIZE 100000000
#define MAX_ORDER 10
#define MIN_BLOCK_SIZE 128
#define MAX_BLOCK_SIZE (MIN_BLOCK_SIZE << MAX_ORDER)
#define TOT_BLOCKS_SIZE (32*MAX_BLOCK_SIZE)
enum Method {as_smalloc, as_scalloc};

struct MallocMetadata
{
    size_t data_size;
    size_t block_size; //data_size + sizeof(metadata)
    bool is_free;
    Method method;
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
        metadata->method = Method::as_smalloc;
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
        Method method = metadata->method;
        size_t block_size = metadata->block_size;
        size_t new_block_size = block_size >> 1;
        size_t lvl = _calc_lvl(block_size);

        if (lvl == 0)
            return NULL;

        delete_block(metadata);

        MallocMetadata::metadata_init_block(metadata, new_block_size);
        buddy_metadata  = _get_buddy(metadata);
        MallocMetadata::metadata_init_block(buddy_metadata,new_block_size);

        metadata->method = method;
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
        Method method = metadata->method;
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

        new_metadata->method = method;
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
        uintptr_t buddy_addr = ((uintptr_t)addr) ^ (block_size); 

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

long getHugePageSize() {
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    long size = 0;

    while (std::getline(meminfo, line)) {
        if (line.find("Hugepagesize:") != std::string::npos) {
            size_t pos = line.find_first_of("0123456789");
            if (pos != std::string::npos) {
                size = std::stol(line.substr(pos)) * 1024;  // Convert from kB to bytes
            }
            break;
        }
    }

    return size;
}

size_t _align_size(size_t size, size_t to) {
    if (size % to == 0)
        return size;

    size_t add = to - size % to;
    return size + add;
}

void* _smalloc(size_t size, Method method = Method::as_smalloc, size_t calloc_block_size = 0 )
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
        if (size >= (1 << 22) && method == Method::as_smalloc ) // 4MB
        {
            size_t hugepage_size = getHugePageSize(); // gonna change hugepage size?
            needed_size = _align_size(needed_size, hugepage_size); // need to align size for the munmap later

            metadata_addr = mmap(NULL, needed_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);

        }else if (calloc_block_size > (1 << 20)  && method == Method::as_scalloc) //2MB
        {
            size_t hugepage_size = getHugePageSize(); 
            needed_size = _align_size(needed_size, hugepage_size); // need to align size for the munmap later
            
            metadata_addr = mmap(NULL, needed_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);

        }else //regular
            metadata_addr = mmap(NULL, needed_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        if (metadata_addr == MAP_FAILED)
            return NULL;

        metadata = (MallocMetadata*)metadata_addr;
        MallocMetadata::metadata_init_block(metadata, needed_size);
        metadata->is_free = false;
        metadata->method = method;
        manager.add_new_block(metadata);

        data_addr = (char*)metadata_addr + sizeof(MallocMetadata);
        return data_addr;
    }

    metadata = manager.find_free_block(needed_size);
    if (metadata == NULL)
        return NULL;
    

    manager.mark_alloc_bin_block(metadata);
    metadata->method = method;
    
    data_addr = (char*)metadata + sizeof(MallocMetadata);
    return data_addr;
}

void* scalloc(size_t num, size_t size)
{
    void* data_addr = _smalloc(size*num, Method::as_scalloc, size);
    if (data_addr == NULL)
        return NULL;
    
    std::memset(data_addr, 0, size*num);
    return data_addr;
}

void* smalloc(size_t size)
{
    return _smalloc(size, Method::as_smalloc);
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
        return _smalloc(size);

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
            
        newp = _smalloc(size, old_metadata->method, size); //TODO if was originally calloced then the new size is the size of the block?
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
            newp = _smalloc(size, old_metadata->method);
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