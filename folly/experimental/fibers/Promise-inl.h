/*
 * Copyright 2016 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <folly/experimental/fibers/Baton.h>

namespace folly {
namespace fibers {

template <class T>
Promise<T>::Promise(folly::Try<T>& value, Baton& baton)
    : value_(&value), baton_(&baton) {}

template <class T>
Promise<T>::Promise(Promise&& other) noexcept
    : value_(other.value_), baton_(other.baton_) {
  other.value_ = nullptr;
  other.baton_ = nullptr;
}

template <class T>
Promise<T>& Promise<T>::operator=(Promise&& other) {
  std::swap(value_, other.value_);
  std::swap(baton_, other.baton_);
  return *this;
}

template <class T>
void Promise<T>::throwIfFulfilled() const {
  if (!value_) {
    throw std::logic_error("promise already fulfilled");
  }
}

template <class T>
Promise<T>::~Promise() {
  if (value_) {
    setException(folly::make_exception_wrapper<std::logic_error>(
        "promise not fulfilled"));
  }
}

template <class T>
void Promise<T>::setException(folly::exception_wrapper e) {
  setTry(folly::Try<T>(e));
}

template <class T>
void Promise<T>::setTry(folly::Try<T>&& t) {
  throwIfFulfilled();

  *value_ = std::move(t);
  value_ = nullptr;

  // Baton::post has to be the last step here, since if Promise is not owned by
  // the posting thread, it may be destroyed right after Baton::post is called.
  baton_->post();
}

template <class T>
template <class M>
void Promise<T>::setValue(M&& v) {
  static_assert(!std::is_same<T, void>::value, "Use setValue() instead");

  setTry(folly::Try<T>(std::forward<M>(v)));
}

template <class T>
void Promise<T>::setValue() {
  static_assert(std::is_same<T, void>::value, "Use setValue(value) instead");

  setTry(folly::Try<void>());
}

template <class T>
template <class F>
void Promise<T>::setWith(F&& func) {
  setTry(makeTryWith(std::forward<F>(func)));
}

template <class T>
template <class F>
typename Promise<T>::value_type Promise<T>::await(F&& func) {
  folly::Try<value_type> result;
  std::exception_ptr funcException;

  Baton baton;
  baton.wait([&func, &result, &baton, &funcException]() mutable {
    try {
      func(Promise<value_type>(result, baton));
    } catch (...) {
      // Save the exception, but still wait for baton to be posted by user code
      // or promise destructor.
      funcException = std::current_exception();
    }
  });

  if (UNLIKELY(funcException != nullptr)) {
    std::rethrow_exception(funcException);
  }

  return folly::moveFromTry(result);
}
}
}
