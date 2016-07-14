////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2016 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Jan Steemann
////////////////////////////////////////////////////////////////////////////////

#include "RevisionCache.h"
#include "Basics/Exceptions.h"
#include "Basics/ReadLocker.h"
#include "Basics/WriteLocker.h"
#include "ReadCache/RevisionCacheChunk.h"
    
#include "Logger/Logger.h"

using namespace arangodb;

// the global read-cache for documents
RevisionCache::RevisionCache(size_t defaultChunkSize, size_t totalTargetSize, GarbageCollectionCallback const& callback)
    : _defaultChunkSize(defaultChunkSize), 
      _totalTargetSize(totalTargetSize), 
      _totalAllocated(0), 
      _callback(callback) { 

  TRI_ASSERT(defaultChunkSize >= 1024);

  _freeList.reserve(2);
  _usedList.reserve(static_cast<size_t>(1.2 * (totalTargetSize / defaultChunkSize)));
}

RevisionCache::~RevisionCache() {
  // free all chunks
  WRITE_LOCKER(locker, _chunksLock);
  for (auto& chunk : _usedList) {
    delete chunk;
  }
  for (auto& chunk : _freeList) {
    delete chunk;
  }
}

size_t RevisionCache::totalAllocated() {
  READ_LOCKER(locker, _chunksLock);
  return _totalAllocated;
}

// stores a revision in the read-cache, acquiring a lease
// the collection id is prepended to the actual data in order to quickly access
// the shard-local hash for the revision when cleaning up the chunk 
RevisionReader RevisionCache::storeAndLease(uint64_t collectionId, uint8_t const* data, size_t length) {
  while (true) {
    RevisionCacheChunk* chunk = nullptr;
    {
      READ_LOCKER(locker, _chunksLock);

      // try to find a suitable chunk in the freelist
      if (!_freeList.empty()) {
        chunk = _freeList.back();
        //LOG(ERR) << "FREELIST IS NOT EMPTY. FOUND CHUNK " << chunk << " ON FREELIST";
        try {
          //LOG(ERR) << "TRYING STORE AND LEASE";
          return chunk->storeAndLease(collectionId, data, length);
          // all is well
        } catch (arangodb::basics::Exception const& ex) {
          //LOG(ERR) << "STORE AND LEASE FAILED";
          if (ex.code() == TRI_ERROR_LOCKED) {
          //LOG(ERR) << "CHUNK IS LOCKED";
            // chunk is being garbage collected at the moment
            // we cannot use this chunk but need to create a new one
            chunk = nullptr;
          } 
        }
      }
    }

    // no suitable chunk found...
    // add a new chunk capable of holding at least the target length
    addChunk(length, chunk);
    
    // and try insertion again in next iteration
    //LOG(ERR) << "REPEATING";
  }
}

// stores a revision in the read-cache, without acquiring a lease
// the collection id is prepended to the actual data in order to quickly access
// the shard-local hash for the revision when cleaning up the chunk 
void RevisionCache::store(uint64_t collectionId, uint8_t const* data, size_t length) {
  while (true) {
    RevisionCacheChunk* chunk = nullptr;
    {
      READ_LOCKER(locker, _chunksLock);

      // try to find a suitable chunk in the freelist
      if (!_freeList.empty()) {
        chunk = _freeList.back();
        try {
          chunk->store(collectionId, data, length);
          // all is well
          return;
        } catch (arangodb::basics::Exception const& ex) {
          if (ex.code() == TRI_ERROR_LOCKED) {
            // chunk is being garbage collected at the moment
            // we cannot use this chunk but need to create a new one
            chunk = nullptr;
          } 
        }
      }
    }
    
    // no suitable chunk found...
    // add a new chunk capable of holding at least the target length
    addChunk(length, chunk);

    // and try insertion again in next iteration
  }
}
  
// run the garbage collection with the intent to free unused chunks
bool RevisionCache::garbageCollect() {
  std::unique_ptr<RevisionCacheChunk> gcChunk;

  {
    WRITE_LOCKER(locker, _chunksLock);

    if (_totalAllocated < _totalTargetSize) {
      return false;
    }

    auto it = _usedList.begin();

    while (it != _usedList.end()) {
      if ((*it)->hasReaders()) {
        // there are active readers for this chunk. cannot collect it now!
        ++it;
      } else if ((*it)->hasReferences()) {
        // there are still external references for this chunk. must not clean it up
        ++it;
      } else {
        _usedList.erase(it);
        gcChunk.reset(*it);
        break;
      }
    }
  }

  return garbageCollect(gcChunk);
}

// garbage collect a single chunk
bool RevisionCache::garbageCollect(std::unique_ptr<RevisionCacheChunk>& chunk) {
  if (chunk == nullptr) {
    return false;
  }

  uint32_t const chunkSize = chunk->size();
  chunk->garbageCollect(_callback);
  // TODO: implement resetting and reusage for chunks
  chunk.reset(); // destroy the chunk
       
  { 
    WRITE_LOCKER(locker, _chunksLock);
    // adjust statistics
    _totalAllocated -= chunkSize;
  }

  return true;
}

// calculate the size for a new chunk
size_t RevisionCache::newChunkSize(size_t dataLength) const noexcept {
  return (std::max)(_defaultChunkSize, RevisionCacheChunk::physicalSize(dataLength));
}

// adds a new chunk, capable of storing at least dataLength
// additionally this will move fullChunk into the used list if it is still
// contained in the free list
void RevisionCache::addChunk(size_t dataLength, RevisionCacheChunk* fullChunk) {
  // create a new chunk with the required size
  size_t const targetSize = newChunkSize(dataLength);
  std::unique_ptr<RevisionCacheChunk> chunk(buildChunk(targetSize));

  std::unique_ptr<RevisionCacheChunk> gcChunk;

  // perform operation under a mutex so concurrent create requests
  // don't pile up here
  {
    WRITE_LOCKER(locker, _chunksLock);
 
    // start off by moving the full chunk to the used list, and by removing
    // it from the free list 
    if (fullChunk != nullptr) {
      for (auto it = _freeList.crbegin(), end = _freeList.crend(); it != end; ++it) {
        if ((*it) == fullChunk) {
          // found it. move it to the used list
          _usedList.emplace(fullChunk);
          // and erase it from the free list
          _freeList.erase(std::next(it).base()); // ugly workaround because we're using a reverse iterator
          break;
        }
      }
    }

    // we only need to add a new chunk if no one else has done this yet
    if (!_freeList.empty()) {
      // somebody else has added a chunk already
      return;
    }

    // check if we need to garbage collect
    if (_totalAllocated >= _totalTargetSize) {
      // try garbage collecting another chunk
      auto it = _usedList.begin();

      while (it != _usedList.end()) {
        if ((*it)->hasReaders()) {
          // there are active readers for this chunk. cannot collect it now!
          ++it;
        } else if ((*it)->hasReferences()) {
          // there are still external references for this chunk. must not clean it up
          ++it;
        } else {
          _usedList.erase(it);
          gcChunk.reset(*it);
          break;
        }
      }
    }

    // add chunk to the list of free chunks
    _freeList.push_back(chunk.get());
    _totalAllocated += targetSize;
  }
  chunk.release();
 
  // garbage collect outside of lock 
  garbageCollect(gcChunk);
}

// creates a chunk
RevisionCacheChunk* RevisionCache::buildChunk(size_t targetSize) {
  return new RevisionCacheChunk(static_cast<uint32_t>(targetSize));
}
