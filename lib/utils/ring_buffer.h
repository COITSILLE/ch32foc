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
    RingBuffer(type* rb, uint32_t size);
    /**
     * @brief Push one item.
     * @param data Item to store.
     * @return None.
     */
    void push(type data);
    /**
     * @brief Pop one item.
     * @param None.
     * @return Popped item or default value.
     */
    type pop();
private:
    type* buffer;
    uint32_t size;

    volatile uint32_t head_idx;
    volatile uint32_t tail_idx;
};

#ifndef RING_BUFFER_IPP
#include "ring_buffer.ipp"
#endif /*RING_BUFFER_IPP*/

#endif /*__cplusplus*/