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

#include "RevisionCacheChunk.h"
#include "Basics/MutexLocker.h"
#include "Utils/Transaction.h"
#include "VocBase/CollectionRevisionsCache.h"

using namespace arangodb;

/// @brief create an empty usage object, pointing to nothing
ChunkUsageStatus::ChunkUsageStatus() : _chunk(nullptr), _offset(0) {}

/// @brief create a valid usage object, pointing to vpack in the read cache
ChunkUsageStatus::ChunkUsageStatus(RevisionCacheChunk* chunk, uint32_t offset, uint32_t expectedVersion) 
        : _chunk(chunk), _offset(offset) {
  TRI_ASSERT(_chunk != nullptr);

  if (!_chunk->use(expectedVersion)) {
    // invalid
    _chunk = nullptr;
  }
}

ChunkUsageStatus::~ChunkUsageStatus() {
  if (_chunk != nullptr) {
    _chunk->release();
  }
}

ChunkUsageStatus::ChunkUsageStatus(ChunkUsageStatus&& other) : _chunk(other._chunk), _offset(other._offset) {
  other._chunk = nullptr;
}
  
ChunkUsageStatus& ChunkUsageStatus::operator=(ChunkUsageStatus&& other) {
  if (_chunk == nullptr || _chunk == other._chunk) {
    _chunk = other._chunk;
    other._chunk = nullptr;
    _offset = other._offset;
  } else {
    _chunk->release();
    _chunk = other._chunk;
    _offset = other._offset;
  }
  return *this;
}
  
uint8_t const* ChunkUsageStatus::vpack() const {
  if (_chunk == nullptr) {
    return nullptr;
  }
  return _chunk->data() + _offset;
}

// note: version must be 1 or higher as version 0 means "WAL"
RevisionCacheChunk::RevisionCacheChunk(uint32_t size)
    : _data(nullptr), _writeOffset(0), _size(size), _versionAndRefCount(buildVersion(1)) {
  _data = new uint8_t[size];
}

RevisionCacheChunk::~RevisionCacheChunk() {
  delete[] _data;
}

uint32_t RevisionCacheChunk::advanceWritePosition(uint32_t size) {
  uint32_t offset;
  {
    MUTEX_LOCKER(locker, _writeMutex);

    if (_writeOffset + size > _size) {
      // chunk would be full
      return UINT32_MAX; // means: chunk is full
    }
    offset = _writeOffset;
    _writeOffset += size;
  }

  return offset;
}

void RevisionCacheChunk::findRevisions(std::vector<TRI_voc_rid_t>& foundRevisions) {
  foundRevisions.clear();

  // no need for write mutex here as the chunk is read-only once fully
  // written to
  uint8_t const* data = _data;
  uint8_t const* end = _data + _writeOffset;
  
  while (data < end) {
    // peek into document at the current position
    VPackSlice slice(data);
    TRI_ASSERT(slice.isObject());

    TRI_voc_rid_t rid = Transaction::extractRevFromDocument(slice);
    foundRevisions.emplace_back(rid);
  }
}

void RevisionCacheChunk::invalidate() { 
  // increasing the chunk's version number
  // this will make all future reads ignore data in the chunk
  while (true) {
    uint64_t old = _versionAndRefCount.load();
    uint64_t desired = increaseVersion(old);
    if (_versionAndRefCount.compare_exchange_strong(old, desired)) {
      break;
    }
  } 
}
  
bool RevisionCacheChunk::use(uint32_t expectedVersion) noexcept {
  uint64_t old = _versionAndRefCount.fetch_add(1);
  if (versionPart(old) != expectedVersion) {
    // version mismatch. now decrease refcount again
    --_versionAndRefCount;
    return false;
  }
  // we have increased the reference counter here
  return true;
}

void RevisionCacheChunk::release() noexcept {
  uint64_t old = _versionAndRefCount.fetch_sub(1);
  TRI_ASSERT(refCountPart(old) > 0);
  // TODO
}

bool RevisionCacheChunk::isUsed() noexcept {
  uint64_t old = _versionAndRefCount.load();
  return refCountPart(old) > 0;
}
  
