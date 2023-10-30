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
    size_t curr_char_offs = 0;
    int element_idx = buffer->out_offs;

    if (buffer->out_offs == buffer->in_offs && buffer->full == false) {
        // The circular buffer is empty, so there's no data to return.
        return NULL;
    }

    do {
        int curr_ele_size = buffer->entry[element_idx].size;

        // check if current element will contain that offset?
        if(curr_char_offs + curr_ele_size > char_offset) {
            // element is found!
            *entry_offset_byte_rtn = char_offset - curr_char_offs;
                
            return &(buffer->entry[element_idx]);   	    
        }
        else {
            curr_char_offs += buffer->entry[element_idx].size; 
        }    

        // jump to next element of the buffer
        element_idx = (element_idx + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    } while(element_idx != buffer->in_offs);

    return NULL;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
const char* aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    const char *ptr = NULL;
    
    ptr = buffer->entry[buffer->in_offs].buffptr;

    // Adding the new entry irrespective of the state of the buffer (as overwrite is allowed)
    buffer->entry[buffer->in_offs] = *(add_entry); 

    // In the case of full buffer we have to increase the out_offs as now the 2nd last element is 
    // the last pushed element
    if (true == buffer->full) {
        buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }

    // incrementing in_offs in all situations as the new element will be added in every invokation
    buffer->in_offs = (buffer->in_offs + 1) % (AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED);

    // updating the buffer full status after every invokation
    buffer->full = (buffer->in_offs == buffer->out_offs) ? true : false;

    return ptr;
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
