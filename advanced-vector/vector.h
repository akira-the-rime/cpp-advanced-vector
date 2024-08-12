#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

template <typename T>
class RawMemory final {
public:
    RawMemory() noexcept = default;
    RawMemory(const RawMemory& other) = delete;
    RawMemory& operator=(const RawMemory& other) = delete;

    RawMemory(RawMemory&& other) noexcept {
        this->buffer_ = std::exchange(other.buffer_, nullptr);
        this->capacity_ = other.capacity_;
        other.capacity_ = 0;
    }

    RawMemory& operator=(RawMemory&& other) noexcept {
        if (this != &other) {
            RawMemory temp(std::move(other));
            this->Swap(temp);
        }

        return *this;
    }

    explicit RawMemory(std::size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }

    ~RawMemory() noexcept {
        Deallocate(buffer_);
    }

    T* operator+(std::size_t offset) noexcept {
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(std::size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](std::size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    T& operator[](std::size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(this->buffer_, other.buffer_);
        std::swap(this->capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    std::size_t Capacity() const noexcept {
        return capacity_;
    }

private:
    static T* Allocate(std::size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    std::size_t capacity_ = 0;
};

template <typename T>
class Vector {
public:
    using iterator = T*;
    using const_iterator = const T*;

    Vector() noexcept = default;

    explicit Vector(std::size_t size) 
        : data_(size)
        , size_(size) {

        std::uninitialized_value_construct_n(data_.GetAddress(), size);
    }

    Vector(const Vector& other) 
        : data_(other.size_)
        , size_(other.size_) {

        std::uninitialized_copy_n(other.data_.GetAddress(), other.size_, this->data_.GetAddress());
    }

    Vector& operator=(const Vector& other) {
        if (this->Capacity() < other.size_) {
            Vector temp(other);
            this->Swap(temp);
        }
        else {
            std::ranges::copy(other.begin(), other.begin() + std::min(this->size_, other.size_), this->begin());

            if (this->size_ > other.size_) {
                std::destroy_n(this->data_ + other.size_, this->size_ - other.size_);
            }
            else {
                std::uninitialized_copy_n(other.data_ + this->size_, other.size_ - this->size_, this->data_ + this->size_);
            }

            this->size_ = other.size_;
        }

        return *this;
    }

    Vector(Vector&& other) noexcept
        : data_(std::move(other.data_))
        , size_(other.size_) {

        other.size_ = 0;
    }

    Vector& operator=(Vector&& other) noexcept {
        other.size_ = 0;
        this->data_ = std::move(other.data_);
        return *this;
    }

    ~Vector() noexcept {
        std::destroy_n(data_.GetAddress(), size_);
    }

    std::size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        std::ptrdiff_t distance = std::ranges::distance(this->begin(), pos);

        if (size_ == data_.Capacity()) {
            ReallocateAndEmbed(distance, std::forward<Args>(args)...);
        }
        else {
            Embed(pos, distance, std::forward<Args>(args)...);
        }

        ++size_;
        return data_ + distance;
    }

    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        return *(Emplace(this->cend(), std::forward<Args>(args)...));
    }

    iterator Erase(const_iterator pos) noexcept {
        auto distance = std::ranges::distance(this->begin(), pos);
        std::ranges::move(data_ + distance + 1, this->end(), data_ + distance);
        std::destroy_at(this->end() - 1);

        --size_;
        return data_ + distance;
    }

    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }

    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::move(value));
    }

    void PopBack() noexcept {
        std::destroy_at(data_ + (--size_));
    }

    void PushBack(const T& value) {
        EmplaceBack(value);
    }

    void PushBack(T&& value) {
        EmplaceBack(std::move(value));
    }

    void Reserve(std::size_t capacity) {
        if (capacity <= data_.Capacity()) {
            return;
        }

        RawMemory<T> new_data(capacity);
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
        }
        else {
            std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
        }

        std::destroy_n(data_.GetAddress(), size_);
        data_.Swap(new_data);
    }

    void Resize(std::size_t new_size) {
        if (new_size <= data_.Capacity()) {
            if (new_size < size_) {
                std::destroy_n(data_ + new_size, size_ - new_size);
            }
            else {
                std::uninitialized_value_construct_n(data_ + size_, new_size - size_);
            }
        }
        else {
            RawMemory<T> new_data(new_size);
            if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
                std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
                std::uninitialized_value_construct_n(new_data + size_, new_size - size_);
            }
            else {
                std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
                std::uninitialized_value_construct_n(new_data + size_, new_size - size_);
            }

            std::destroy_n(data_.GetAddress(), size_);
            data_.Swap(new_data);
        }

        size_ = new_size;
    }

    std::size_t Size() const noexcept {
        return size_;
    }

    void Swap(Vector& other) noexcept {
        this->data_.Swap(other.data_);
        std::swap(this->size_, other.size_);
    }

    const T& operator[](std::size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](std::size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

    iterator begin() noexcept {
        return data_.GetAddress();
    }

    iterator end() noexcept {
        return data_ + size_;
    }

    const_iterator begin() const noexcept {
        return data_.GetAddress();
    }

    const_iterator end() const noexcept {
        return data_ + size_;
    }

    const_iterator cbegin() const noexcept {
        return data_.GetAddress();
    }

    const_iterator cend() const noexcept {
        return data_ + size_;
    }

private:
    template <class... Args>
    void ReallocateAndEmbed(std::ptrdiff_t distance, Args&&... args) {
        RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
        std::construct_at(new_data + distance, std::forward<Args>(args)...);

        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(data_.GetAddress(), distance, new_data.GetAddress());
            std::uninitialized_move_n(data_ + distance, size_ - distance, new_data + distance + 1);
        }
        else {
            std::uninitialized_copy_n(data_.GetAddress(), distance, new_data.GetAddress());
            std::uninitialized_copy_n(data_ + distance, size_ - distance, new_data + distance + 1);
        }

        std::destroy_n(data_.GetAddress(), size_);
        data_.Swap(new_data);
    }

    template <class... Args>
    void Embed(const_iterator pos, std::ptrdiff_t distance, Args&&... args) {
        if (pos == this->end()) {
            std::construct_at(this->end(), std::forward<Args>(args)...);
        }
        else {
            T arg(std::forward<Args>(args)...);
            std::construct_at(this->end(), std::move(*(this->end() - 1)));

            std::ranges::move_backward(this->begin() + distance, this->end() - 1, this->end());
            *(data_ + distance) = std::move(arg);
        }
    }

    RawMemory<T> data_;
    std::size_t size_ = 0;
};