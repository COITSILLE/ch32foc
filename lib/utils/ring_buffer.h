#pragma once

#ifdef __cplusplus
extern "C" {
#include "sys_interface.h"

#include "rtos_handler.h"

#endif

#ifdef __cplusplus
}

template<typename type>
class RingBuffer {
public:
    /**
     * @brief Create a ring buffer view.
     * @param rb Backing storage.
     * @param size Storage size.
     * @return None.
     */
    RingBuffer(type* rb, uint32_t size) : size(size) {
        this->buffer = rb;
        head_idx = 0;
        tail_idx = 0;
    }
    /**
     * @brief Push one item.
     * @param data Item to store.
     * @return None.
     */
    void push(type data){
    #ifdef USING_FREERTOS
        UBaseType_t old_mask = portSET_INTERRUPT_MASK_FROM_ISR();
    #endif
        if(this->head_idx == (this->tail_idx + 1) % this->size){
            // Buffer is full, overwrite the oldest data
            this->head_idx = (this->head_idx + 1) % this->size;
        }
        this->buffer[this->tail_idx] = data;
        this->tail_idx = (this->tail_idx + 1) % this->size;
    #ifdef USING_FREERTOS
        portCLEAR_INTERRUPT_MASK_FROM_ISR(old_mask);
    #endif
    }

    void push_multi(type* data, uint32_t count){
    #ifdef USING_FREERTOS
        UBaseType_t old_mask = portSET_INTERRUPT_MASK_FROM_ISR();
    #endif
        uint32_t available_space = (this->head_idx + this->size - this->tail_idx - 1) % this->size;

        uint32_t push_size = count;
        if (count >= this->size) push_size = this->size - 1;

        type* push_data_ptr = data + count - push_size;// discard the parts that will be overwritten

        uint32_t first_chunk = this->size - this->tail_idx;
        if (first_chunk > push_size){
            memcpy(&this->buffer[this->tail_idx], push_data_ptr, push_size * sizeof(type));
        }
        else{
            memcpy(&this->buffer[this->tail_idx], push_data_ptr, first_chunk * sizeof(type));
            memcpy(&this->buffer[0], push_data_ptr + first_chunk, (push_size - first_chunk) * sizeof(type));
        }
        this->tail_idx = (this->tail_idx + push_size) % this->size;
        if (count > available_space) this->head_idx = (this->tail_idx + 1) % this->size;
        
    #ifdef USING_FREERTOS
        portCLEAR_INTERRUPT_MASK_FROM_ISR(old_mask);
    #endif
    }
    /**
     * @brief Pop one item.
     * @param None.
     * @return Popped item or default value.
     */
    type pop(bool* empty = nullptr){
    #ifdef USING_FREERTOS
        UBaseType_t old_mask = portSET_INTERRUPT_MASK_FROM_ISR();
    #endif
        if(this->head_idx == this->tail_idx){
            // Buffer is empty, return default value
    #ifdef USING_FREERTOS
            portCLEAR_INTERRUPT_MASK_FROM_ISR(old_mask);
    #endif
            if(empty) *empty = true;
            return type();
        }
        type data = this->buffer[this->head_idx];
        this->head_idx = (this->head_idx + 1) % this->size;
    #ifdef USING_FREERTOS
        portCLEAR_INTERRUPT_MASK_FROM_ISR(old_mask);
    #endif
        if(empty) *empty = false;
        return data;
    }
private:
    type* buffer;
    uint32_t size;

    volatile uint32_t head_idx;
    volatile uint32_t tail_idx;
};

#endif /*__cplusplus*/