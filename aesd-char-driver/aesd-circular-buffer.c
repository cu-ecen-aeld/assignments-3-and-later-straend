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
#else
#include <string.h>
#endif
#include <stdio.h>

#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
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
        cur_len += cur->size;
        
        // check if searched offset is in current range
        if (char_offset < cur_len){
            //printf("MATCH: offset: %ld\t cur_len: %ld\n", char_offset, cur_len);
            //printf("\t size: %ld\t cur-len - size: %ld\n", cur->size, cur_len - cur->size);
            *entry_offset_byte_rtn = cur->size - (cur_len - char_offset);
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
*/
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    // Check if we have written over the limit
    // and need to wrap around to the start again
    if (buffer->in_offs >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
        // return to first entry
        buffer->in_offs = 0;
        if(!buffer->full){
            //printf("OVERRUN\n");
            buffer->full = true;
            
            // increase out pointer to next item and discard the oldest value
            if (++buffer->out_offs >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) buffer->out_offs = 0;    
        }
    }

    
    // insert into buffer
    buffer->entry[buffer->in_offs] = *add_entry;
    //printf("ADD\n");
    //printf("\t in: %d\t out: %d\n", buffer->in_offs, buffer->out_offs);
    //printf("\t %s", buffer->entry[buffer->in_offs].buffptr);
    
    buffer->in_offs += 1;
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
