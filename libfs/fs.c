#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "disk.h"
#include "fs.h"

#define FAT_EOC 0xFFFF
#define fat_length(fat_amount) ((fat_amount)*BLOCK_SIZE/2)

/* TODO: Phase 1 */
typedef struct superblock{
    uint64_t signature;
    uint16_t total_amount;
    uint16_t root_idx;
    uint16_t data_idx;
    uint16_t data_amount;
    uint8_t FAT_amount;
    uint8_t padding[4079];
}__attribute__((__packed__)) superblock;

typedef uint16_t* FAT;

typedef struct root_entry{
    uint8_t filename[16];
    uint32_t filesize;
    uint16_t data_start;
    uint8_t padding[10];
}__attribute__((__packed__)) root_entry;

typedef struct file_descriptor{
    int root_idx;
    size_t offset;
    uint16_t block_idx;
}file_descriptor;

typedef root_entry* root_dir;

superblock super_block;

FAT fat_array = NULL;

root_dir root;

file_descriptor open_files[FS_OPEN_MAX_COUNT];

bool change_table = false;

bool mounted = false;

int root_next_free = 0;
int fat_next_free = 1;

int block_to_buffer(size_t block, void* buff, size_t length){
    for (size_t i = block; i < block + length; i++){
        if(block_read(i, buff + (i - block) * BLOCK_SIZE) == -1)
            return -1;
    }
    return 0;
}

int buffer_to_block(size_t block, void* buff, size_t length){
    for (size_t i = block; i < block + length; i++){
        if(block_write(i, buff + (i - block) * BLOCK_SIZE) == -1)
            return -1;
    }
    return 0;
}

int fat_free(){
    int count = 0;
    for (int i = 1; i < fat_length(super_block.FAT_amount); i++){
        if (fat_array[i] == 0)
            count++;
    }
    return count;
}

int root_free(){
    int count = 0;
    for (int i = 0; i < FS_FILE_MAX_COUNT; i++){
        if (root[i].filename[0] == '\0')
            count++;
    }
    return count;
}

int check_filename(const char* filename){
    bool valid_file = false;
    for (int i = 0; i < FS_FILENAME_LEN; i++){
        if (filename[i] == '\0'){
            valid_file = true;
            break;
        }
    }
    if (!valid_file)
        return -1;
    return 0;
}

int check_root(const char *filename){
    bool first_empty = true;
    for (int i = 0; i < FS_FILE_MAX_COUNT; i++){
        if (root[i].filename[0] == '\0'){
            if (first_empty){
                root_next_free = i;
                first_empty = false;
            }
        }else{
            if(strcmp((char*) root[i].filename, filename) == 0)
                return -1;
        }
    }
    if (first_empty)
        return -1;
    return 0;
}

int find_file(const char *filename){
    for (int i = 0; i < FS_FILE_MAX_COUNT; i++){
        if (strcmp((char*) root[i].filename, filename) == 0){
            return i;
        }
    }
    return -1;
}

int get_num_blocks(size_t count, size_t offset){
    int diff = BLOCK_SIZE - offset % BLOCK_SIZE;
    // if count fits within the current block
    if (count < diff)
        return 1;
    //count evenly fits within multiple blocks
    else if ((count - diff) % BLOCK_SIZE == 0)
        // add 1 to count for the current block
        return (count - diff) / BLOCK_SIZE + 1;
    else
        //add 2 to count for current block and the extra bytes left when we divide
        return (count - diff) / BLOCK_SIZE + 2;
}

size_t check_and_copy(char* block, char* buff, size_t count, size_t block_offset, size_t buff_offset, int fd){
    for(int j = 0; j < count; j ++){
        if (block[block_offset + j] != EOF)
            buff[buff_offset + j] = block[block_offset + j];
        else{
            buff[buff_offset + j] = EOF;
            open_files[fd].offset += j;
            return j + 1;
        }
    }
    open_files[fd].offset += count;
    return count;
}

size_t write_bytes(char* block, char* buff, size_t count, size_t block_offset, size_t buff_offset, int fd){
    bool reach_end = false;
    for(int j = 0; j < count; j ++){
        if(block[block_offset + j] == EOF){
            reach_end = true;
        }
        block[block_offset + j] = buff[buff_offset + j];
    }
    if (reach_end && count < BLOCK_SIZE){
        block[count] = EOF;
    }
    open_files[fd].offset += count;
    return count;
}


int find_fat_next_free(){
    for (int i = 1; i < fat_length(super_block.FAT_amount); i++){
        if (fat_array[i] == 0)
            return i;
    }
    return -1;
}

int fs_mount(const char *diskname)
{
	/* TODO: Phase 1 */
    char* signature = "ECS150FS";
    if(block_disk_open(diskname) == -1)
        return -1;
    
    block_read(0, (void*) &super_block);
    
    if (memcmp((char*) &super_block.signature, signature, 8) != 0)
        return -1;
    
    if (super_block.total_amount != block_disk_count())
        return -1;
    

    fat_array = malloc(fat_length(super_block.FAT_amount) * sizeof(uint16_t));
    if (fat_array == NULL)
        return -1;
    if (block_to_buffer(1, fat_array, super_block.FAT_amount) == -1)
        return -1;

    root = malloc(FS_FILE_MAX_COUNT * sizeof(root_entry));
    if (block_read(super_block.root_idx, (void*) root) == -1)
        return -1;
    
    for (int i = 0; i < FS_OPEN_MAX_COUNT; i++)
        open_files[i].root_idx = -1;
    
    mounted = true;
    return 0;
}

int fs_umount(void)
{
	/* TODO: Phase 1 */
    // to do check file des
    
    if (change_table){
        if (buffer_to_block(1, fat_array, super_block.FAT_amount) == -1)
            return -1;
        if (block_write(super_block.root_idx, root) == -1)
            return -1;
    }
    
    mounted = false;
    return block_disk_close();
}

int fs_info(void)
{
	/* TODO: Phase 1 */
    if (!mounted)
        return -1;
        
    printf("FS Info:\n");
    printf("total_blk_count=%d\n", super_block.total_amount);
    printf("fat_blk_count=%d\n", super_block.FAT_amount);
    printf("rdir_blk=%d\n",super_block.root_idx);
    printf("data_blk=%d\n",super_block.data_idx);
    printf("data_blk_count=%d\n",super_block.data_amount);
    printf("fat_free_ratio=%d/%d\n",fat_free(), fat_length(super_block.FAT_amount));
    printf("rdir_free_ratio=%d/%d\n",root_free(), FS_FILE_MAX_COUNT);
    
    return 0;
}

int fs_create(const char *filename)
{
	/* TODO: Phase 2 */
    if (check_filename(filename) == -1)
        return -1;
    
    if (check_root(filename) == -1)
        return -1;
    
    strcpy((char*) root[root_next_free].filename, filename);
    root[root_next_free].filesize = 0;
    root[root_next_free].data_start = FAT_EOC;
    
    change_table = true;
    return 0;
}

int fs_delete(const char *filename)
{
	/* TODO: Phase 2 */
    
    if (check_filename(filename) == -1)
        return -1;
    
    // to do check open
    
    int pos = find_file(filename);
    
    if (pos == -1)
        return -1;
    
    root[pos].filename[0] = '\0';
    change_table = true;
    return 0;
    
}

int fs_ls(void)
{
	/* TODO: Phase 2 */
    if (!mounted)
        return -1;
    
    printf("FS Ls:\n");
    for (int i = 0; i < FS_FILE_MAX_COUNT; i++){
        if (root[i].filename[0] != '\0')
            printf("file: %s, size: %d, data_blk: %d\n", root[i].filename, root[i].filesize, root[i].data_start);
    }
    return 0;
}

int fs_open(const char *filename)
{
	/* TODO: Phase 3 */
    if (check_filename(filename) == -1)
        return -1;
    
    int pos = find_file(filename);
    
    if (pos == -1)
        return -1;
    
    for (int i = 0; i < FS_OPEN_MAX_COUNT; i ++){
        if ( open_files[i].root_idx == -1){
            open_files[i].root_idx = pos;
            open_files[i].offset = 0;
            open_files[i].block_idx = root[pos].data_start;
            return i;
        }
    }
    
    return -1;
}

int fs_close(int fd)
{
	/* TODO: Phase 3 */
    if (fd < 0 || fd >= FS_OPEN_MAX_COUNT)
        return -1;
    
    if (open_files[fd].root_idx == -1)
        return -1;
    
    open_files[fd].root_idx = -1;
    
    return 0;
}

int fs_stat(int fd)
{
	/* TODO: Phase 3 */
    if (fd < 0 || fd >= FS_OPEN_MAX_COUNT)
        return -1;
    
    if (open_files[fd].root_idx == -1)
        return -1;
    
    return root[open_files[fd].root_idx].filesize;
}

int fs_lseek(int fd, size_t offset)
{
	/* TODO: Phase 3 */
    if (fd < 0 || fd >= FS_OPEN_MAX_COUNT)
        return -1;
    
    if (open_files[fd].root_idx == -1)
        return -1;
    
    if (offset > root[open_files[fd].root_idx].filesize)
        return -1;
    
    open_files[fd].offset = offset;
    
    return 0;
}

int fs_write(int fd, void *buf, size_t count)
{
	/* TODO: Phase 4 */
    int i, fat_free_idx;
    int amount_wrote = 0;
    size_t res;
    
    if (fd < 0 || fd >= FS_OPEN_MAX_COUNT)
        return -1;
    
    if (open_files[fd].root_idx == -1)
        return -1;
    
    char *buffer = malloc(BLOCK_SIZE*sizeof(char));
    int num_blocks = get_num_blocks(count, open_files[fd].offset);
    int diff = BLOCK_SIZE - (open_files[fd].offset % BLOCK_SIZE);
    
    block_read(open_files[fd].block_idx + 2 + super_block.FAT_amount, (void*) buffer);
    if (count > diff){
        write_bytes(buffer, buf, diff, open_files[fd].offset, 0, fd);
        amount_wrote += diff;
        block_write(open_files[fd].block_idx + 2 + super_block.FAT_amount, (void*) buffer);
    }
    else{
        write_bytes(buffer, buf, count, open_files[fd].offset, 0, fd);
        amount_wrote += count;
        block_write(open_files[fd].block_idx + 2 + super_block.FAT_amount, (void*) buffer);
        return amount_wrote;
    }
    
    for (i = 1; i < num_blocks - 1; i++){
        
        //check if it is the last block
        if (fat_array[open_files[fd].block_idx] == FAT_EOC){
            
            fat_free_idx = find_fat_next_free();
            if (fat_free_idx == -1)
                return amount_wrote;
            else{
                fat_array[open_files[fd].block_idx] = fat_free_idx;
                fat_array[fat_free_idx] = FAT_EOC;
            }
        }
        //get next block idx
        open_files[fd].block_idx = fat_array[open_files[fd].block_idx];
            
        //read the block directly into buff
        block_write(open_files[fd].block_idx + 2 + super_block.FAT_amount, buf + amount_read);
        open_files[fd].offset += BLOCK_SIZE;
        amount_wrote += BLOCK_SIZE;
    }
    if (num_blocks > 1){
        //check if it is the last block
        if (fat_array[open_files[fd].block_idx] == FAT_EOC){
            
            fat_free_idx = find_fat_next_free();
            if (fat_free_idx == -1)
                return amount_wrote;
            else{
                fat_array[open_files[fd].block_idx] = fat_free_idx;
                fat_array[fat_free_idx] = FAT_EOC;
                memset(buffer, 0, BLOCK_SIZE);
            }
        }else{
            open_files[fd].block_idx = fat_array[open_files[fd].block_idx];
            block_read(open_files[fd].block_idx + 2 + super_block.FAT_amount, (void*) buffer);
        }
        write_bytes(buffer, buf, count - amount_wrote, open_files[fd].offset, amount_wrote, fd);
        block_write(open_files[fd].block_idx + 2 + super_block.FAT_amount, (void*) buffer);
        
        amount_wrote = count ;
    }
    
    return amount_wrote;
}

int fs_read(int fd, void *buf, size_t count)
{
	/* TODO: Phase 4 */
    int i;
    int amount_read = 0;
    size_t res;
    if (fd < 0 || fd >= FS_OPEN_MAX_COUNT)
        return -1;
    
    if (open_files[fd].root_idx == -1)
        return -1;
    
    char *buffer = malloc(BLOCK_SIZE*sizeof(char));
    
    int num_blocks = get_num_blocks(count, open_files[fd].offset);
    int diff = BLOCK_SIZE - (open_files[fd].offset % BLOCK_SIZE);
    
    block_read(open_files[fd].block_idx + 2 + super_block.FAT_amount, (void*) buffer);
    if (count > diff){
        res = check_and_copy(buffer, buf, diff, open_files[fd].offset, 0, fd);
        if (res != diff)
            return res;
        else
            amount_read += res;
    }
    else{
        res = check_and_copy(buffer, buf, count, open_files[fd].offset, 0, fd);
        return res;
    }
        
    
    for (i = 1; i < num_blocks - 1; i++){
        //get next block idx
        open_files[fd].block_idx = fat_array[open_files[fd].block_idx];
        //check if it is the last block
        if (fat_array[open_files[fd].block_idx] == FAT_EOC){
            block_read(open_files[fd].block_idx + 2 + super_block.FAT_amount, (void*) buffer);
            res = check_and_copy(buffer, buf , count - amount_read, 0, amount_read, fd);
            amount_read += res;
            return amount_read;
        }
        //read the block directly into buff
        block_read(open_files[fd].block_idx + 2 + super_block.FAT_amount, buf + amount_read);
        open_files[fd].offset += BLOCK_SIZE;
        amount_read += BLOCK_SIZE;
    }
    if (num_blocks > 1){
        
        open_files[fd].block_idx = fat_array[open_files[fd].block_idx];
        block_read(open_files[fd].block_idx + 2 + super_block.FAT_amount, (void*) buffer);
        res = check_and_copy(buffer, buf , count - amount_read, 0, amount_read, fd);
        amount_read += res;
    }
    
    return amount_read;
}

