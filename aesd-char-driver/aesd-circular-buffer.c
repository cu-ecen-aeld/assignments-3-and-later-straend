/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/slab.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"
#include <linux/printk.h>

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, 
 *      or NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    struct aesd_buffer_entry *cur;
    int index = buffer->out_offs;
    size_t cur_len = 0;
    int iterations = 0;
    while(iterations++ < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
        cur = &buffer->entry[index];
        // abort if current entry size is 0
        if (cur->size == 0) return NULL;
        cur_len += cur->size;
        
        // check if searched offset is in current range
        if (char_offset <= cur_len){
            *entry_offset_byte_rtn = cur->size - (cur_len - char_offset);
            printk("reading position: %d", index);

            return cur;
        }
        
        // increase index and wraparound to 0 if needed
        if (++index>=AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) index = 0;
    }
    
    // char_offset is too big, not in our data
    return NULL;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
* @return returns a pointer to old memory that caller should free, or NULL if no entry is overwritten
*/
char *aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    char *ret = NULL;
     if (buffer->in_offs >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
        // return to first entry
        buffer->in_offs = 0;
        if(!buffer->full){
            buffer->full = true;
            
            // increase out pointer to next item and discard the oldest value
            if (++buffer->out_offs >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) buffer->out_offs = 0;    

        }
    }
    // Check if we have written over the limit
    // and need to wrap around to the start again

    printk("Writing to position: %d", buffer->in_offs);
    // We are overwriting old memory here
    // need to free memory
    // buffptr is NULL if we dont have written there before
    ret = buffer->entry[buffer->in_offs].buffptr;

    // insert into buffer
    buffer->entry[buffer->in_offs] = *add_entry;
    //printk("\t in: %d\t out: %d\n", buffer->in_offs, buffer->out_offs);
    //printk(">\t %s", buffer->entry[buffer->in_offs].buffptr);
    buffer->in_offs++;
   
    //if (++buffer->in_offs >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) buffer->in_offs = 0;
    return ret;

}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
