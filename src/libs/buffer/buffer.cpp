#include <cstring>
#include <cstdio>

#include <stdexcept>
#include <utility>

#include "buffer.h"

void buffer::move(buffer &&other)
{
   size_ = other.size_;
   capacity_ = other.capacity_;
   data_ = std::move(other.data_);
   other.size_ = other.capacity_ = 0;
}

void buffer::copy(const buffer &other)
{
   size_ = other.size_;
   capacity_ = other.capacity_;
   data_.reset(new char[capacity_]);
   memcpy(data_.get(), other.data_.get(), size_);   
}

void buffer::grow(size_type min, size_type mul)
{
   if (0 == capacity_) capacity_ = default_size;
   while (capacity_ <= min) capacity_ *= mul;

   char *n_data = new char[capacity_];
   memcpy(n_data, data_.get(), size_);
   data_.reset(n_data);
}

void buffer::print_(const char *format, va_list args)
{
   int printed, free;
   va_list arg_copy;
   bool grew = false;

   if (0 == capacity_) grow();   
   for (;;)
   {
      if (grew) va_end(arg_copy);
      va_copy(arg_copy, args);

      free = capacity_ - size_;
      printed = vsnprintf(data_.get() + size_, free, format, arg_copy);
      if (0 > printed) throw std::runtime_error("vsnprintf() fail.");

      if (printed >= free) 
      {
         if (grew) throw std::runtime_error("buffer reprint failed even after growing.");
         grow(printed + size_);
         continue;
      }
      break;
   }

   va_end(arg_copy);
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

void buffer::mappend(const char *mem, size_type length)
{
   if ((capacity_ - size_) < length + 1) grow(size_ + length + 1);
   memcpy(data_.get() + size_, mem, length);
   size_ += length;
   data_.get()[size_] = '\0';
}

void buffer::append(char ch)
{
   size_type sz = sizeof(char) * 2;
   if ((capacity_ - size_) < sz) grow();
   *(data_.get() + size_) = ch;
   size_++;
   *(data_.get() + size_) = '\0';
}

void buffer::setmem(char *memory, size_type capacity, size_type size)
{
   size_ = size;
   capacity_ = capacity;
   data_.reset(memory);
}
