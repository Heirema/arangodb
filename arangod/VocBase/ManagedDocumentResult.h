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

#ifndef ARANGOD_VOC_BASE_MANAGED_DOCUMENT_RESULT_H
#define ARANGOD_VOC_BASE_MANAGED_DOCUMENT_RESULT_H 1

#include "Basics/Common.h"
#include "VocBase/RevisionCacheChunk.h"

namespace arangodb {
class Transaction;

struct ChunkCache {
  static constexpr size_t CACHE_SIZE = 4;
  RevisionCacheChunk* _chunks[CACHE_SIZE];
  size_t _chunksUsed;

  ChunkCache() : _chunks(), _chunksUsed(0) {}

  void add(RevisionCacheChunk* chunk) {
    TRI_ASSERT(_chunksUsed <= CACHE_SIZE);
    if (_chunksUsed > 0) {
      size_t maxValue = _chunksUsed;
      if (maxValue == CACHE_SIZE) {
        --maxValue;
      }
      for (size_t i = maxValue; i > 0; --i) {
        _chunks[i] = _chunks[i - 1];
      }
    }

    _chunks[0] = chunk;
    if (_chunksUsed < CACHE_SIZE) {
      ++_chunksUsed;
    }
    TRI_ASSERT(_chunksUsed <= CACHE_SIZE);
  }

  bool contains(RevisionCacheChunk* chunk) const {
    for (size_t i = 0; i < _chunksUsed; ++i) {
      if (_chunks[i] == chunk) {
        return true;
      }
    }
    return false;
  }
};

class ManagedDocumentResult {
 public:
  ManagedDocumentResult() = delete;
  ManagedDocumentResult(ManagedDocumentResult const& other) = delete;
  ManagedDocumentResult(ManagedDocumentResult&& other) = delete;
  ManagedDocumentResult& operator=(ManagedDocumentResult const& other);
  ManagedDocumentResult& operator=(ManagedDocumentResult&& other) = delete;

  explicit ManagedDocumentResult(Transaction*);
  ~ManagedDocumentResult();

  inline uint8_t const* vpack() const { 
    TRI_ASSERT(_vpack != nullptr); 
    return _vpack; 
  }
  
  void add(ChunkProtector& protector, TRI_voc_rid_t revisionId);
  void addExisting(ChunkProtector& protector, TRI_voc_rid_t revisionId);

  bool hasSeenChunk(RevisionCacheChunk* chunk) const { return _chunkCache.contains(chunk); }
  TRI_voc_rid_t lastRevisionId() const { return _lastRevisionId; }
  uint8_t const* lastVPack() const { return _vpack; }

  //void clear();

 private:
  Transaction* _trx;
  uint8_t const* _vpack;
  TRI_voc_rid_t _lastRevisionId;
  ChunkCache _chunkCache;
};

class ManagedMultiDocumentResult {
 public:
  ManagedMultiDocumentResult() = delete;
  ManagedMultiDocumentResult(ManagedMultiDocumentResult const& other) = delete;
  ManagedMultiDocumentResult(ManagedMultiDocumentResult&& other) = delete;
  ManagedMultiDocumentResult& operator=(ManagedMultiDocumentResult const& other) = delete;
  ManagedMultiDocumentResult& operator=(ManagedMultiDocumentResult&& other) = delete;

  explicit ManagedMultiDocumentResult(Transaction*);
  ~ManagedMultiDocumentResult();
  
  bool hasSeenChunk(RevisionCacheChunk* chunk) const { return _chunkCache.contains(chunk); }

  inline uint8_t const* at(size_t position) const {
    return _results.at(position); 
  }
  
  inline uint8_t const* operator[](size_t position) const {
    return _results[position]; 
  }
  
  bool empty() const { return _results.empty(); }
  size_t size() const { return _results.size(); }
  void clear() { _results.clear(); _lastRevisionId = 0; }
  void reserve(size_t size) { _results.reserve(size); }
  uint8_t const*& back() { return _results.back(); }
  uint8_t const* const& back() const { return _results.back(); }
  
  std::vector<uint8_t const*>::iterator begin() { return _results.begin(); }
  std::vector<uint8_t const*>::iterator end() { return _results.end(); }
  
  void add(ChunkProtector& protector, TRI_voc_rid_t revisionId);
  void addExisting(ChunkProtector& protector, TRI_voc_rid_t revisionId);
  
  TRI_voc_rid_t lastRevisionId() const { return _lastRevisionId; }
  uint8_t const* lastVPack() const { return _results.back(); }
  
  inline uint8_t const* vpack() const { 
    TRI_ASSERT(!_results.empty());
    return _results.back();
  }
 
 private:
  Transaction* _trx;
  std::vector<uint8_t const*> _results;
  TRI_voc_rid_t _lastRevisionId;
  ChunkCache _chunkCache;
};

}

#endif
