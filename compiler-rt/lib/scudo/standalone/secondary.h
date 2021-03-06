//===-- secondary.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_SECONDARY_H_
#define SCUDO_SECONDARY_H_

#include "common.h"
#include "list.h"
#include "mutex.h"
#include "stats.h"
#include "string_utils.h"

namespace scudo {

// This allocator wraps the platform allocation primitives, and as such is on
// the slower side and should preferably be used for larger sized allocations.
// Blocks allocated will be preceded and followed by a guard page, and hold
// their own header that is not checksummed: the guard pages and the Combined
// header should be enough for our purpose.

namespace LargeBlock {

struct Header {
  LargeBlock::Header *Prev;
  LargeBlock::Header *Next;
  uptr BlockEnd;
  uptr MapBase;
  uptr MapSize;
  MapPlatformData Data;
};

constexpr uptr getHeaderSize() {
  return roundUpTo(sizeof(Header), 1U << SCUDO_MIN_ALIGNMENT_LOG);
}

static Header *getHeader(uptr Ptr) {
  return reinterpret_cast<Header *>(Ptr - getHeaderSize());
}

static Header *getHeader(const void *Ptr) {
  return getHeader(reinterpret_cast<uptr>(Ptr));
}

} // namespace LargeBlock

class MapAllocatorNoCache {
public:
  void initLinkerInitialized(UNUSED s32 ReleaseToOsInterval) {}
  void init(UNUSED s32 ReleaseToOsInterval) {}
  bool retrieve(UNUSED uptr Size, UNUSED LargeBlock::Header **H) {
    return false;
  }
  bool store(UNUSED LargeBlock::Header *H) { return false; }
  static bool canCache(UNUSED uptr Size) { return false; }
  void disable() {}
  void enable() {}
};

template <uptr MaxEntriesCount = 32U, uptr MaxEntrySize = 1UL << 19>
class MapAllocatorCache {
public:
  // Fuchsia doesn't allow releasing Secondary blocks yet. Note that 0 length
  // arrays are an extension for some compilers.
  // FIXME(kostyak): support (partially) the cache on Fuchsia.
  static_assert(!SCUDO_FUCHSIA || MaxEntriesCount == 0U, "");

  void initLinkerInitialized(s32 ReleaseToOsInterval) {
    ReleaseToOsIntervalMs = ReleaseToOsInterval;
  }
  void init(s32 ReleaseToOsInterval) {
    memset(this, 0, sizeof(*this));
    initLinkerInitialized(ReleaseToOsInterval);
  }

  bool store(LargeBlock::Header *H) {
    bool EntryCached = false;
    bool EmptyCache = false;
    const u64 Time = getMonotonicTime();
    {
      ScopedLock L(Mutex);
      if (EntriesCount == MaxEntriesCount) {
        if (IsFullEvents++ == 4U)
          EmptyCache = true;
      } else {
        for (uptr I = 0; I < MaxEntriesCount; I++) {
          if (Entries[I].Block)
            continue;
          if (I != 0)
            Entries[I] = Entries[0];
          Entries[0].Block = reinterpret_cast<uptr>(H);
          Entries[0].BlockEnd = H->BlockEnd;
          Entries[0].MapBase = H->MapBase;
          Entries[0].MapSize = H->MapSize;
          Entries[0].Time = Time;
          EntriesCount++;
          EntryCached = true;
          break;
        }
      }
    }
    if (EmptyCache)
      empty();
    else if (ReleaseToOsIntervalMs >= 0)
      releaseOlderThan(Time -
                       static_cast<u64>(ReleaseToOsIntervalMs) * 1000000);
    return EntryCached;
  }

  bool retrieve(uptr Size, LargeBlock::Header **H) {
    ScopedLock L(Mutex);
    if (EntriesCount == 0)
      return false;
    for (uptr I = 0; I < MaxEntriesCount; I++) {
      if (!Entries[I].Block)
        continue;
      const uptr BlockSize = Entries[I].BlockEnd - Entries[I].Block;
      if (Size > BlockSize)
        continue;
      if (Size < BlockSize - getPageSizeCached() * 4U)
        continue;
      *H = reinterpret_cast<LargeBlock::Header *>(Entries[I].Block);
      Entries[I].Block = 0;
      (*H)->BlockEnd = Entries[I].BlockEnd;
      (*H)->MapBase = Entries[I].MapBase;
      (*H)->MapSize = Entries[I].MapSize;
      EntriesCount--;
      return true;
    }
    return false;
  }

  static bool canCache(uptr Size) {
    return MaxEntriesCount != 0U && Size <= MaxEntrySize;
  }

  void disable() { Mutex.lock(); }

  void enable() { Mutex.unlock(); }

private:
  void empty() {
    struct {
      void *MapBase;
      uptr MapSize;
      MapPlatformData Data;
    } MapInfo[MaxEntriesCount];
    uptr N = 0;
    {
      ScopedLock L(Mutex);
      for (uptr I = 0; I < MaxEntriesCount; I++) {
        if (!Entries[I].Block)
          continue;
        MapInfo[N].MapBase = reinterpret_cast<void *>(Entries[I].MapBase);
        MapInfo[N].MapSize = Entries[I].MapSize;
        MapInfo[N].Data = Entries[I].Data;
        Entries[I].Block = 0;
        N++;
      }
      EntriesCount = 0;
      IsFullEvents = 0;
    }
    for (uptr I = 0; I < N; I++)
      unmap(MapInfo[I].MapBase, MapInfo[I].MapSize, UNMAP_ALL,
            &MapInfo[I].Data);
  }

  void releaseOlderThan(u64 Time) {
    struct {
      uptr Block;
      uptr BlockSize;
      MapPlatformData Data;
    } BlockInfo[MaxEntriesCount];
    uptr N = 0;
    {
      ScopedLock L(Mutex);
      if (!EntriesCount)
        return;
      for (uptr I = 0; I < MaxEntriesCount; I++) {
        if (!Entries[I].Block || !Entries[I].Time)
          continue;
        if (Entries[I].Time > Time)
          continue;
        BlockInfo[N].Block = Entries[I].Block;
        BlockInfo[N].BlockSize = Entries[I].BlockEnd - Entries[I].Block;
        BlockInfo[N].Data = Entries[I].Data;
        Entries[I].Time = 0;
        N++;
      }
    }
    for (uptr I = 0; I < N; I++)
      releasePagesToOS(BlockInfo[I].Block, 0, BlockInfo[I].BlockSize,
                       &BlockInfo[I].Data);
  }

  struct CachedBlock {
    uptr Block;
    uptr BlockEnd;
    uptr MapBase;
    uptr MapSize;
    MapPlatformData Data;
    u64 Time;
  };

  HybridMutex Mutex;
  CachedBlock Entries[MaxEntriesCount];
  u32 EntriesCount;
  uptr LargestSize;
  u32 IsFullEvents;
  s32 ReleaseToOsIntervalMs;
};

template <class CacheT> class MapAllocator {
public:
  void initLinkerInitialized(GlobalStats *S, s32 ReleaseToOsInterval = -1) {
    Cache.initLinkerInitialized(ReleaseToOsInterval);
    Stats.initLinkerInitialized();
    if (LIKELY(S))
      S->link(&Stats);
  }
  void init(GlobalStats *S, s32 ReleaseToOsInterval = -1) {
    memset(this, 0, sizeof(*this));
    initLinkerInitialized(S, ReleaseToOsInterval);
  }

  void *allocate(uptr Size, uptr AlignmentHint = 0, uptr *BlockEnd = nullptr,
                 bool ZeroContents = false);

  void deallocate(void *Ptr);

  static uptr getBlockEnd(void *Ptr) {
    return LargeBlock::getHeader(Ptr)->BlockEnd;
  }

  static uptr getBlockSize(void *Ptr) {
    return getBlockEnd(Ptr) - reinterpret_cast<uptr>(Ptr);
  }

  void getStats(ScopedString *Str) const;

  void disable() {
    Mutex.lock();
    Cache.disable();
  }

  void enable() {
    Cache.enable();
    Mutex.unlock();
  }

  template <typename F> void iterateOverBlocks(F Callback) const {
    for (const auto &H : InUseBlocks)
      Callback(reinterpret_cast<uptr>(&H) + LargeBlock::getHeaderSize());
  }

  static uptr canCache(uptr Size) { return CacheT::canCache(Size); }

private:
  CacheT Cache;

  HybridMutex Mutex;
  DoublyLinkedList<LargeBlock::Header> InUseBlocks;
  uptr AllocatedBytes;
  uptr FreedBytes;
  uptr LargestSize;
  u32 NumberOfAllocs;
  u32 NumberOfFrees;
  LocalStats Stats;
};

// As with the Primary, the size passed to this function includes any desired
// alignment, so that the frontend can align the user allocation. The hint
// parameter allows us to unmap spurious memory when dealing with larger
// (greater than a page) alignments on 32-bit platforms.
// Due to the sparsity of address space available on those platforms, requesting
// an allocation from the Secondary with a large alignment would end up wasting
// VA space (even though we are not committing the whole thing), hence the need
// to trim off some of the reserved space.
// For allocations requested with an alignment greater than or equal to a page,
// the committed memory will amount to something close to Size - AlignmentHint
// (pending rounding and headers).
template <class CacheT>
void *MapAllocator<CacheT>::allocate(uptr Size, uptr AlignmentHint,
                                     uptr *BlockEnd, bool ZeroContents) {
  DCHECK_GE(Size, AlignmentHint);
  const uptr PageSize = getPageSizeCached();
  const uptr RoundedSize =
      roundUpTo(Size + LargeBlock::getHeaderSize(), PageSize);

  if (AlignmentHint < PageSize && CacheT::canCache(RoundedSize)) {
    LargeBlock::Header *H;
    if (Cache.retrieve(RoundedSize, &H)) {
      if (BlockEnd)
        *BlockEnd = H->BlockEnd;
      void *Ptr = reinterpret_cast<void *>(reinterpret_cast<uptr>(H) +
                                           LargeBlock::getHeaderSize());
      if (ZeroContents)
        memset(Ptr, 0, H->BlockEnd - reinterpret_cast<uptr>(Ptr));
      const uptr BlockSize = H->BlockEnd - reinterpret_cast<uptr>(H);
      {
        ScopedLock L(Mutex);
        InUseBlocks.push_back(H);
        AllocatedBytes += BlockSize;
        NumberOfAllocs++;
        Stats.add(StatAllocated, BlockSize);
        Stats.add(StatMapped, H->MapSize);
      }
      return Ptr;
    }
  }

  MapPlatformData Data = {};
  const uptr MapSize = RoundedSize + 2 * PageSize;
  uptr MapBase =
      reinterpret_cast<uptr>(map(nullptr, MapSize, "scudo:secondary",
                                 MAP_NOACCESS | MAP_ALLOWNOMEM, &Data));
  if (UNLIKELY(!MapBase))
    return nullptr;
  uptr CommitBase = MapBase + PageSize;
  uptr MapEnd = MapBase + MapSize;

  // In the unlikely event of alignments larger than a page, adjust the amount
  // of memory we want to commit, and trim the extra memory.
  if (UNLIKELY(AlignmentHint >= PageSize)) {
    // For alignments greater than or equal to a page, the user pointer (eg: the
    // pointer that is returned by the C or C++ allocation APIs) ends up on a
    // page boundary , and our headers will live in the preceding page.
    CommitBase = roundUpTo(MapBase + PageSize + 1, AlignmentHint) - PageSize;
    const uptr NewMapBase = CommitBase - PageSize;
    DCHECK_GE(NewMapBase, MapBase);
    // We only trim the extra memory on 32-bit platforms: 64-bit platforms
    // are less constrained memory wise, and that saves us two syscalls.
    if (SCUDO_WORDSIZE == 32U && NewMapBase != MapBase) {
      unmap(reinterpret_cast<void *>(MapBase), NewMapBase - MapBase, 0, &Data);
      MapBase = NewMapBase;
    }
    const uptr NewMapEnd = CommitBase + PageSize +
                           roundUpTo((Size - AlignmentHint), PageSize) +
                           PageSize;
    DCHECK_LE(NewMapEnd, MapEnd);
    if (SCUDO_WORDSIZE == 32U && NewMapEnd != MapEnd) {
      unmap(reinterpret_cast<void *>(NewMapEnd), MapEnd - NewMapEnd, 0, &Data);
      MapEnd = NewMapEnd;
    }
  }

  const uptr CommitSize = MapEnd - PageSize - CommitBase;
  const uptr Ptr =
      reinterpret_cast<uptr>(map(reinterpret_cast<void *>(CommitBase),
                                 CommitSize, "scudo:secondary", 0, &Data));
  LargeBlock::Header *H = reinterpret_cast<LargeBlock::Header *>(Ptr);
  H->MapBase = MapBase;
  H->MapSize = MapEnd - MapBase;
  H->BlockEnd = CommitBase + CommitSize;
  H->Data = Data;
  if (BlockEnd)
    *BlockEnd = CommitBase + CommitSize;
  {
    ScopedLock L(Mutex);
    InUseBlocks.push_back(H);
    AllocatedBytes += CommitSize;
    if (LargestSize < CommitSize)
      LargestSize = CommitSize;
    NumberOfAllocs++;
    Stats.add(StatAllocated, CommitSize);
    Stats.add(StatMapped, H->MapSize);
  }
  return reinterpret_cast<void *>(Ptr + LargeBlock::getHeaderSize());
}

template <class CacheT> void MapAllocator<CacheT>::deallocate(void *Ptr) {
  LargeBlock::Header *H = LargeBlock::getHeader(Ptr);
  const uptr Block = reinterpret_cast<uptr>(H);
  const uptr CommitSize = H->BlockEnd - Block;
  {
    ScopedLock L(Mutex);
    InUseBlocks.remove(H);
    FreedBytes += CommitSize;
    NumberOfFrees++;
    Stats.sub(StatAllocated, CommitSize);
    Stats.sub(StatMapped, H->MapSize);
  }
  if (CacheT::canCache(CommitSize) && Cache.store(H))
    return;
  void *Addr = reinterpret_cast<void *>(H->MapBase);
  const uptr Size = H->MapSize;
  MapPlatformData Data = H->Data;
  unmap(Addr, Size, UNMAP_ALL, &Data);
}

template <class CacheT>
void MapAllocator<CacheT>::getStats(ScopedString *Str) const {
  Str->append(
      "Stats: MapAllocator: allocated %zu times (%zuK), freed %zu times "
      "(%zuK), remains %zu (%zuK) max %zuM\n",
      NumberOfAllocs, AllocatedBytes >> 10, NumberOfFrees, FreedBytes >> 10,
      NumberOfAllocs - NumberOfFrees, (AllocatedBytes - FreedBytes) >> 10,
      LargestSize >> 20);
}

} // namespace scudo

#endif // SCUDO_SECONDARY_H_
