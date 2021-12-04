// Copyright 2017 The Ray Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "ray/util/memory.h"

#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>
#include <cstring>
#include <thread>
#include <vector>

namespace ray {

uint8_t *pointer_logical_and(const uint8_t *address, uintptr_t bits) {
  uintptr_t value = reinterpret_cast<uintptr_t>(address);
  return reinterpret_cast<uint8_t *>(value & bits);
}

void parallel_memcopy(uint8_t *dst, const uint8_t *src, int64_t nbytes,
                      uintptr_t block_size, int num_threads) {
  // The compiler generates an atomic operation to ensure the static local variable is
  // initialized only once even with concurrent callers. Only one thread pool will be
  // created.
  static auto &threadpool =
      *new boost::asio::thread_pool(num_threads);
  uint8_t *left = pointer_logical_and(src + block_size - 1, ~(block_size - 1));
  uint8_t *right = pointer_logical_and(src + nbytes, ~(block_size - 1));
  int64_t num_blocks = (right - left) / block_size;

  // Update right address
  right = right - (num_blocks % num_threads) * block_size;

  // Now we divide these blocks between available threads. The remainder is
  // handled on the main thread.
  int64_t chunk_size = (right - left) / num_threads;
  int64_t prefix = left - src;
  int64_t suffix = src + nbytes - right;
  // Now the data layout is | prefix | k * num_threads * block_size | suffix |.
  // We have chunk_size = k * block_size, therefore the data layout is
  // | prefix | num_threads * chunk_size | suffix |.
  // Each thread gets a "chunk" of k blocks.

  // Start all threads first and handle leftovers while threads run.
  for (int i = 0; i < num_threads; i++) {
    boost::asio::post(threadpool, std::bind(std::memcpy, dst + prefix + i * chunk_size,
                                            left + i * chunk_size, chunk_size));
  }

  std::memcpy(dst, src, prefix);
  std::memcpy(dst + prefix + num_threads * chunk_size, right, suffix);

  threadpool.join();
}

}  // namespace ray
