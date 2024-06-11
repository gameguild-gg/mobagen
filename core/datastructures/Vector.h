#ifndef MOBAGEN_VECTOR_H
#define MOBAGEN_VECTOR_H
/***
 *
 * @brief This is just me trying to improve my skills dont use this as a vector replacement
 *
 * @tparam T data type to hold inside the container
 */
template <typename T> struct Vector {
private:
  T* buffer;
  // Constructors
  // default (1)
  // vector();
  // explicit vector (const allocator_type& alloc);

  // fill (2)
  // explicit vector (size_type n, const allocator_type& alloc = allocator_type());
  // vector (size_type n, const value_type& val, const allocator_type& alloc = allocator_type());

  // range (3)
  // template <class InputIterator>  vector (InputIterator first, InputIterator last, const allocator_type& alloc = allocator_type());

  // copy (4)
  // vector (const vector& x);vector (const vector& x, const allocator_type& alloc);

  // move (5)
  // vector (vector&& x);
  //  vector (vector&& x, const allocator_type& alloc);

  // initializer list (6)
  // vector (initializer_list<value_type> il, const allocator_type& alloc = allocator_type());

  ~Vector();
};

#endif  // MOBAGEN_VECTOR_H
