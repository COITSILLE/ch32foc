#pragma once

#ifdef __cplusplus

template<typename type>
RingBuffer<type>::RingBuffer(type* rb, uint32_t size) : size(size) {
    this->buffer = rb;
    head_idx = 0;
    tail_idx = 0;
}

template<typename type>
void RingBuffer<type>::push(type data){
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

template<typename type>
type RingBuffer<type>::pop(){
#ifdef USING_FREERTOS
    UBaseType_t old_mask = portSET_INTERRUPT_MASK_FROM_ISR();
#endif
    if(this->head_idx == this->tail_idx){
        // Buffer is empty, return default value
#ifdef USING_FREERTOS
        portCLEAR_INTERRUPT_MASK_FROM_ISR(old_mask);
#endif
        return type();
    }
    type data = this->buffer[this->head_idx];
    this->head_idx = (this->head_idx + 1) % this->size;
#ifdef USING_FREERTOS
    portCLEAR_INTERRUPT_MASK_FROM_ISR(old_mask);
#endif
    return data;
}
#endif /*__cplusplus*/