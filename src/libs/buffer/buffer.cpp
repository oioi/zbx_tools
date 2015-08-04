#include <cstring>
#include <cstdio>

#include <stdexcept>
#include <utility>

#include "buffer.h"

buffer::buffer(buffer &&other)
{
   size_ = other.size_;
   capacity_ = other.capacity_;
   data_ = std::move(other.data_);
}

buffer::buffer(const buffer &other)
{
   size_ = other.size_;
   capacity_ = other.capacity_;
   data_.reset(new char[capacity_]);
   memcpy(data_.get(), other.data_.get(), size_);
}

void buffer::grow(size_type min, size_type mul)
{
   if (0 == capacity_) capacity_ = default_size;
   while (capacity_ < min) capacity_ *= mul;

   char *n_data = new char[capacity_];
   memcpy(n_data, data_.get(), size_);
   data_.reset(n_data);
}

void buffer::print_(const char *format, va_list args)
{
   int printed, free;
   if (0 == capacity_) grow();

   for (;;)
   {
      free = capacity_ - size_;
      printed = vsnprintf(data_.get() + size_, free, format, args);

      if (0 > printed) throw std::runtime_error("vsnprintf() fail.");
      if (printed >= free) { grow(printed + size_); continue; }
      break;
   }
   size_ += printed;
}

void buffer::print(const char *format, ...)
{
   va_list args;
   va_start(args, format);
   vprint(format, args);
   va_end(args);
}

void buffer::append(const char *format, ...)
{
   va_list args;
   va_start(args, format);
   print_(format, args);
   va_end(args);
}

void buffer::append(const char *mem, size_type length)
{
   if ((capacity_ - size_) < length) grow(size_ + length);
   memcpy(data_.get() + size_, mem, length);
   size_ += length;
}

void buffer::append(char ch)
{
   size_type sz = sizeof(char);
   if ((capacity_ - size_) < sz) grow();
   *(data_.get() + size_) = ch;
   size_++;
}

void buffer::setmem(char *memory, size_type capacity, size_type size)
{
   size_ = size;
   capacity_ = capacity;
   data_.reset(memory);
}
