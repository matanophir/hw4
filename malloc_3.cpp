
#include <unistd.h>
#include <cstring>
#include <cstdint>

#define MAX_SIZE 100000000
#define MAX_ORDER 10
#define MIN_BLOCK_SIZE 128
#define TOT_BLOCKS_SIZE 4194304

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
    LevelManager level_manager[MAX_ORDER + 1];
    size_t num_free_blocks;
    size_t num_free_bytes;
    size_t num_allocated_blocks;
    size_t num_allocated_bytes;
    size_t num_meta_data_bytes;
    size_t size_meta_data;

    BlockManager() : num_free_blocks(0), num_free_bytes(0), num_allocated_blocks(0), num_allocated_bytes(0), num_meta_data_bytes(0), size_meta_data(sizeof(MallocMetadata)) {
        for (int i = 0; i < MAX_ORDER + 1; i++)
        {
            level_manager[i].head = NULL;
        }
        
    };

    void init()
    {
        void *current_brk;
        size_t to_align, max_block_size;
        MallocMetadata* metadata;

        max_block_size = MIN_BLOCK_SIZE << MAX_ORDER;

        current_brk = sbrk(0); // Get current break

        to_align = TOT_BLOCKS_SIZE - (uintptr_t)current_brk%TOT_BLOCKS_SIZE;

        if (to_align != 0)
            sbrk(to_align);

        current_brk = sbrk(TOT_BLOCKS_SIZE);
        to_align = (uintptr_t)current_brk % TOT_BLOCKS_SIZE;

        for (size_t i = 0; i < 32; i++)
        {
            metadata = (MallocMetadata*)current_brk;
            MallocMetadata::metadata_init_block(metadata, max_block_size);

            current_brk = (char*)current_brk + max_block_size;
        }
    }

    void data_add_block(MallocMetadata* metadata)
    {
        if (metadata->is_free){
            ++num_free_blocks;
            num_free_bytes += metadata->data_size;
        }
        ++num_allocated_blocks;
        num_allocated_bytes += metadata->data_size;
        num_meta_data_bytes += sizeof(MallocMetadata);
    }
    void data_remove_block(MallocMetadata* metadata)
    {
        if (metadata->is_free){
            --num_free_blocks;
            num_free_bytes -= metadata->data_size;
        }
        --num_allocated_blocks;
        num_allocated_bytes -= metadata->data_size;
        num_meta_data_bytes -= sizeof(MallocMetadata);
    }




    private:
    MallocMetadata* _get_buddy(MallocMetadata* metadata)
    {
        uintptr_t buddy_addr = (uintptr_t)metadata ^ metadata->block_size; // TODO check that the conversion is valid
        return (MallocMetadata *)buddy_addr;
    }


    size_t _calc_lvl(int size)
    {
        int result = 0;
        int temp = size;
        while (temp >>= 1)
            result++;

        if ((1 << result) < size)
            ++result;
        return result - 7 < 0 ? 0 : result - 7;
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
                if ((metadata > iter && iter->next == NULL) || metadata > iter && metadata < iter->next){
                    metadata->next = iter->next;
                    metadata->prev = iter;
                    iter->next = metadata;
                    if (metadata->next != NULL)
                        metadata->next->prev = metadata;
                }
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

    public:
    // return NULL if didn't find
    MallocMetadata *find_free_block(size_t size)
    {
        MallocMetadata* found_block;
        size_t lvl = _calc_lvl(size + sizeof(MallocMetadata));
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

        size_t lvl = _calc_lvl(metadata->block_size);

        if (lvl > MAX_ORDER) //TODO change later
            return;
        
        _insert(metadata);
        data_add_block(metadata);
    }

    void delete_block(MallocMetadata *metadata)
    {
        if (metadata == NULL)
            return;
        
        _remove(metadata);
        data_remove_block(metadata);
            
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

        data_remove_block(metadata);

        if (is_free)
            _remove(metadata);
        

        buddy_metadata  = _get_buddy(metadata);
        MallocMetadata::metadata_init_block(metadata, new_block_size);
        MallocMetadata::metadata_init_block(buddy_metadata,new_block_size);

        if (is_free == false){
            metadata->is_free = false;
        } else {
            _insert(metadata);
        }
        _insert(buddy_metadata);

        data_add_block(metadata);
        data_add_block(buddy_metadata);

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

        if (buddy_metadata->is_free == false) //can't join
            return NULL;
        
        data_remove_block(metadata);
        data_remove_block(buddy_metadata);

        MallocMetadata::metadata_init(new_metadata,new_block_size);

        if (is_free == false)
            new_metadata->is_free = false;
        
        data_add_block(new_metadata);
    }

};

BlockManager manager = BlockManager();



void* smalloc(size_t size)
{
    static bool to_alloc = true;
    if (to_alloc){
        manager.init();
        
    }
    if (size == 0 || size > MAX_SIZE)
        return NULL;
    
    MallocMetadata* metadata;
    void *base_addr, *data_addr ;

    if (size > (MIN_BLOCK_SIZE << MAX_ORDER)) // handle with mmap
    {

    }

    metadata = manager.find_free_block(size);
    if (metadata == NULL)
        return NULL;
    

    manager.mark_alloc_bin_block(metadata);
    
    data_addr = (char*)metadata + sizeof(MallocMetadata);
    return data_addr;
}

void* scalloc(size_t num, size_t size)
{
    // void* data_addr = smalloc(size*num);
    // if (data_addr == NULL)
    //     return NULL;
    
    // std::memset(data_addr, 0, size*num);
    // return data_addr;
return NULL;
}

void sfree(void* p)
{
    // if (p == NULL)
    //     return;

    // void* metadata_addr = (char*)p - sizeof(MallocMetadata);
    // MallocMetadata* metadata = (MallocMetadata*)metadata_addr;

    // if (metadata->is_free)
    //     return;

    // manager.mark_free(metadata);
}

void* srealloc(void* oldp, size_t size)
{
    // if (size == 0 || size > MAX_SIZE)
    //     return NULL;
        
    // if (oldp == NULL)
    //     return smalloc(size);

    // void* old_metadata_addr = (char*)oldp - sizeof(MallocMetadata);
    // MallocMetadata* old_metadata = (MallocMetadata*)old_metadata_addr;

    // if (size <= old_metadata->data_size)
    // {
    //     return oldp;
    // }

    // MallocMetadata* found_metadata = manager.find_free_block(size);
    // void* newp;

    // if (found_metadata == NULL) //didnt find existing block
    // {
    //     newp = smalloc(size);
    //     if (newp == NULL)
    //         return NULL;

    //     std::memmove(newp, oldp, old_metadata->data_size);
    //     manager.mark_free(old_metadata);
    //     return newp;
    // } else // found existing block
    // {
    //     newp = (char*)found_metadata + sizeof(MallocMetadata);
    //     std::memmove(newp, oldp, old_metadata->data_size);

    //     manager.mark_alloc(found_metadata);
    //     manager.mark_free(old_metadata);
        
    //     return newp;
    // }
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

int main(int argc, char const *argv[])
{
    smalloc(50);
    return 0;
}
