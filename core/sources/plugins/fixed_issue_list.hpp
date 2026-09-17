#pragma once

#include <array>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace mobagen::plugins {

  template <typename Issue, std::size_t Capacity> class FixedIssueList {
    static_assert(Capacity > 0);

  public:
    using value_type = Issue;

    template <bool IsConst> class BasicIterator {
      using OptionalIssue = std::conditional_t<IsConst, const std::optional<Issue>, std::optional<Issue>>;

    public:
      using iterator_concept = std::forward_iterator_tag;
      using iterator_category = std::forward_iterator_tag;
      using value_type = Issue;
      using difference_type = std::ptrdiff_t;
      using reference = std::conditional_t<IsConst, const Issue&, Issue&>;
      using pointer = std::conditional_t<IsConst, const Issue*, Issue*>;

      BasicIterator() = default;
      explicit BasicIterator(OptionalIssue* current) noexcept : current_(current) {}

      [[nodiscard]] reference operator*() const noexcept { return **current_; }
      [[nodiscard]] pointer operator->() const noexcept { return std::addressof(**current_); }

      BasicIterator& operator++() noexcept {
        ++current_;
        return *this;
      }

      BasicIterator operator++(int) noexcept {
        auto previous = *this;
        ++*this;
        return previous;
      }

      friend bool operator==(const BasicIterator&, const BasicIterator&) = default;

    private:
      OptionalIssue* current_{};
    };

    using iterator = BasicIterator<false>;
    using const_iterator = BasicIterator<true>;

    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr std::size_t capacity() const noexcept { return Capacity; }
    [[nodiscard]] iterator begin() noexcept { return iterator{storage_.data()}; }
    [[nodiscard]] const_iterator begin() const noexcept { return const_iterator{storage_.data()}; }
    [[nodiscard]] iterator end() noexcept { return iterator{storage_.data() + size_}; }
    [[nodiscard]] const_iterator end() const noexcept { return const_iterator{storage_.data() + size_}; }
    [[nodiscard]] Issue& operator[](std::size_t index) noexcept { return *storage_[index]; }
    [[nodiscard]] const Issue& operator[](std::size_t index) const noexcept { return *storage_[index]; }

    void push_back(Issue issue) {
      if (size_ == Capacity) throw std::length_error{"fixed issue list capacity exceeded"};
      storage_[size_].emplace(std::move(issue));
      ++size_;
    }

  private:
    std::array<std::optional<Issue>, Capacity> storage_{};
    std::size_t size_{};
  };

}  // namespace mobagen::plugins
