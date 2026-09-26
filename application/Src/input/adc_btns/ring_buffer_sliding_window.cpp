#include "adc_btns/ring_buffer_sliding_window.hpp"

// 显式实例化常用类型
template class RingBufferSlidingWindow<float>;
template class RingBufferSlidingWindow<double>;
template class RingBufferSlidingWindow<int>;
template class RingBufferSlidingWindow<uint32_t>;
template class RingBufferSlidingWindow<uint16_t>;
template class RingBufferSlidingWindow<uint8_t>;
template class RingBufferSlidingWindow<int32_t>;
template class RingBufferSlidingWindow<int16_t>;
template class RingBufferSlidingWindow<int8_t>;
