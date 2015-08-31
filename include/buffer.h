#ifndef COMMON_BUF_H
#define COMMON_BUF_H

#include <cstddef>
#include <cstdarg>
#include <memory>

#include <unistd.h>

class buffer
{
   public:
      typedef ssize_t size_type;
      enum {
         default_mul = 2,
         default_size = 1024 
      };

      explicit buffer(size_type st_capacity = 0) : data_(nullptr), size_(0), capacity_(st_capacity) {
         if (0 != capacity_) grow(st_capacity); }      

      buffer(buffer &&other) { move(std::move(other)); }
      buffer(const buffer &other) { copy(other); }

      buffer& operator=(buffer &&other) { move(std::move(other)); return *this; }
      buffer& operator=(const buffer &other) { copy(other); return *this; }

      const char *data() const { return data_.get(); }
      char *mem() { return data_.get(); }

      size_type size() const { return size_; }
      size_type capacity() const { return capacity_; }

      void pop_back() { if (size_ > 0) { size_--; data_.get()[size_] = '\0'; } }
      void clear() { size_ = 0; }
      void setmem(char *memory, size_type capacity, size_type size);

      void vprint(const char *format, va_list args) { size_ = 0; print_(format, args); }
      void vappend(const char *format, va_list args) { print_(format, args); }

      void print(const char *format, ...) __attribute__((format(printf,2,3)));
      
      void append(char ch);
      void append(const char *format, ...) __attribute__((format(printf,2,3)));

      void mappend(const char *mem, size_type size);      

   private:
      std::unique_ptr<char> data_;
      size_type size_;
      size_type capacity_;

      void move(buffer &&other);      
      void copy(const buffer &other);
      void print_(const char *format, va_list args);      
      void grow(size_type min_capacity = default_size, size_type mul = default_mul);
};

#endif
