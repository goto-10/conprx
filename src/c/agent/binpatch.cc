//- Copyright 2014 the Neutrino authors (see AUTHORS).
//- Licensed under the Apache License, Version 2.0 (see LICENSE).

#include "binpatch.hh"

BEGIN_C_INCLUDES
#include "utils/log.h"
#include "utils/misc-inl.h"
#include "utils/strbuf.h"
END_C_INCLUDES

using namespace conprx;
using namespace tclib;

fat_bool_t MemoryManager::ensure_initialized() {
  return F_TRUE;
}

MemoryManager::~MemoryManager() {
}

PatchRequest::PatchRequest(address_t original, address_t replacement, const char *name)
  : original_(original)
  , replacement_(replacement)
  , name_(name)
  , flags_(0)
  , imposter_(NULL)
  , platform_(NULL)
  , preamble_size_(0) { }

PatchRequest::PatchRequest()
  : original_(NULL)
  , replacement_(NULL)
  , name_(NULL)
  , flags_(0)
  , imposter_(NULL)
  , platform_(NULL)
  , preamble_size_(0) { }

PatchRequest::~PatchRequest() { }

fat_bool_t PatchRequest::prepare_apply(Platform *platform, ProximityAllocator *alloc) {
  platform_ = platform;
  imposter_code_ = alloc->alloc_executable(0, 0, kImposterCodeStubSizeBytes);
  if (imposter_code_.is_empty())
    return F_FALSE;
  // Determine the length of the preamble. This also determines whether the
  // preamble can safely be overwritten, otherwise we'll bail out.
  DEBUG("Preparing %s", name_);
  PreambleInfo pinfo;

  pass_def_ref_t<Redirection> redir;
  F_TRY(platform->instruction_set().prepare_patch(this, alloc, &redir, &pinfo));
  redirection_ = redir;
  size_t size = pinfo.size();
  CHECK_REL("preamble too big", size, <=, kMaxPreambleSizeBytes);
  memcpy(preamble_copy_, original_, pinfo.size());
  preamble_size_ = size;
  return F_TRUE;
}

address_t PatchRequest::get_or_create_imposter() {
  if (imposter_ == NULL)
    write_imposter();
  return imposter_;
}

void PatchRequest::write_imposter() {
  InstructionSet &inst = platform().instruction_set();
  tclib::Blob imposter_memory = imposter_code();
  // Clear the memory to halt such that if the imposter doesn't fill the memory
  // completely there'll be sane code there.
  inst.write_halt(imposter_memory);
  inst.write_imposter(*this, imposter_memory);
  inst.flush_instruction_cache(imposter_memory);
  imposter_ = static_cast<address_t>(imposter_memory.start());
}

PatchSet::PatchSet(Platform &platform, Vector<PatchRequest> requests)
  : platform_(platform)
  , requests_(requests)
  , alloc_(&platform.memory_manager(), 16, 1024 * 1024)
  , status_(NOT_APPLIED)
  , old_perms_(0) { }

fat_bool_t PatchSet::prepare_apply() {
  DEBUG("Preparing to apply patch set");
  if (requests().is_empty()) {
    // Trivially succeed if there are no patches to apply.
    status_ = PREPARED;
    return F_TRUE;
  }
  for (size_t i = 0; i < requests().length(); i++) {
    F_TRY_EXCEPT(requests()[i].prepare_apply(&platform_, alloc()));
      status_ = FAILED;
    F_YRT();
  }
  status_ = PREPARED;
  return F_TRUE;
}

tclib::Blob PatchSet::determine_address_range() {
  if (requests().length() == 0) {
    // There are no patches so the range is empty.
    return tclib::Blob();
  } else {
    // Scan through the patches to determine the range. Note that the result
    // will have its endpoint one past the highest address, hence the +1 at the
    // end.
    address_t lowest;
    address_t highest;
    lowest = highest = requests()[0].original();
    for (size_t i = 1; i < requests().length(); i++) {
      address_t addr = requests()[i].original();
      if (addr < lowest)
        lowest = addr;
      if (addr > highest)
        highest = addr;
    }
    return tclib::Blob(lowest, (highest - lowest) + 1);
  }
}

tclib::Blob PatchSet::determine_patch_range() {
  tclib::Blob addr_range = determine_address_range();
  if (addr_range.is_empty()) {
    return addr_range;
  } else {
    // The amount we're going to write past the last address.
    size_t new_length = addr_range.size() + kMaxPreambleSizeBytes;
    return tclib::Blob(addr_range.start(), new_length);
  }
}

fat_bool_t PatchSet::open_for_patching() {
  DEBUG("Opening original code for writing");
  // Try opening the region for writing.
  tclib::Blob region = determine_patch_range();
  F_TRY_EXCEPT(memory_manager().open_for_writing(region, &old_perms_));
    status_= FAILED;
  F_YRT();
  // Validate that writing works.
  DEBUG("Validating that code is writable");
  if (validate_open_for_patching()) {
    status_ = OPEN;
    return F_TRUE;
  } else {
    status_ = FAILED;
    return F_FALSE;
  }
}

bool PatchSet::validate_open_for_patching() {
  for (size_t i = 0; i < requests().length(); i++) {
    // Try reading the each byte and then writing it back. This should not
    // have any effect but should fail if the memory is not writeable.
    //
    // TODO: check whether making the address volatile is enough for this to
    //   not be optimized away or otherwise tampered with by the compiler.
    volatile address_t addr = requests()[i].original();
    for (size_t i = 0; i < kMaxPreambleSizeBytes; i++) {
      byte_t value = addr[i];
      addr[i] = value;
    }
  }
  return true;
}

fat_bool_t PatchSet::close_after_patching(Status success_status) {
  DEBUG("Closing original code for writing");
  tclib::Blob region = determine_patch_range();
  F_TRY_EXCEPT(memory_manager().close_for_writing(region, old_perms_));
    status_ = FAILED;
  F_YRT();
  instruction_set().flush_instruction_cache(region);
  DEBUG("Successfully closed original code");
  status_ = success_status;
  return F_TRUE;
}

void PatchSet::install_redirects() {
  DEBUG("Installing redirects");
  for (size_t ri = 0; ri < requests().length(); ri++) {
    PatchRequest &request = requests()[ri];
    request.install_redirect();
  }
  DEBUG("Successfully installed redirects");
  status_ = APPLIED_OPEN;
}

void PatchRequest::install_redirect() {
  CHECK_FALSE("no redirection", redirection_.is_null());
  // First clear the whole preamble to halt such that if there is any space left
  // over after writing the redirect it will be valid instructions but shouldn't
  // be useful to anyone.
  tclib::Blob orig_preamble(original_, preamble_size_);
  platform().instruction_set().write_halt(orig_preamble);
  redirection_->write_redirect(original_, replacement_);
}

void PatchSet::revert_redirects() {
  DEBUG("Reverting redirects");
  for (size_t i = 0; i < requests().length(); i++) {
    PatchRequest &request = requests()[i];
    tclib::Blob preamble = request.preamble_copy();
    address_t original = request.original();
    memcpy(original, preamble.start(), preamble.size());
  }
  DEBUG("Successfully reverted redirects");
  status_ = REVERTED_OPEN;
}


fat_bool_t PatchSet::apply() {
  F_TRY(prepare_apply());
  if (!open_for_patching())
    return F_FALSE;
  install_redirects();
  if (!close_after_patching(PatchSet::APPLIED))
    return F_FALSE;
  return F_TRUE;
}

fat_bool_t PatchSet::revert() {
  F_TRY(open_for_patching());
  revert_redirects();
  F_TRY(close_after_patching(PatchSet::NOT_APPLIED));
  return F_TRUE;
}

Platform::Platform(InstructionSet &inst, MemoryManager &memman)
  : inst_(inst)
  , memman_(memman) { }

fat_bool_t Platform::ensure_initialized() {
  return memory_manager().ensure_initialized();
}

// Returns the current platform.
Platform &Platform::get() {
  static Platform *instance = NULL;
  if (instance == NULL) {
    InstructionSet &inst = InstructionSet::get();
    MemoryManager &memman = MemoryManager::get();
    instance = new Platform(inst, memman);
  }
  return *instance;
}

PreambleInfo::PreambleInfo()
  : size_(0)
  , last_instr_(0) { }

void PreambleInfo::populate(size_t size, byte_t last_instr) {
  size_ = size;
  last_instr_ = last_instr;
}

const uint32_t ProximityAllocator::kAnchorOrder[ProximityAllocator::kAnchorCount] = {
    25, 10, 22,  2, 29, 12, 17,  4,
    11, 31, 13,  6,  1,  0, 20, 15,
     7, 27, 16, 23, 30, 14, 19, 18,
     5, 26, 24,  9, 21, 28,  8,  3
};

ProximityAllocator::ProximityAllocator(VirtualAllocator *direct, uint64_t alignment,
    uint64_t block_size)
  : direct_(direct)
  , alignment_(alignment)
  , block_size_(block_size)
  , unrestricted_(NULL) {
  CHECK_TRUE("unaligned alignment", is_power_of_2(alignment));
  CHECK_TRUE("unaligned block size", is_power_of_2(block_size));
}

ProximityAllocator::~ProximityAllocator() {
  if (unrestricted_ != NULL)
    delete_block(unrestricted_);
  BlockMap::iterator iter = anchors_.begin();
  for (; iter != anchors_.end(); iter++)
    delete_block(iter->second);
}

bool ProximityAllocator::delete_block(Block *block) {
  std::vector<tclib::Blob> *chunks = block->to_free();
  for (size_t i = 0; i < chunks->size(); i++)
    direct_->free_block(chunks->at(i));
  delete block;
  return true;
}

ProximityAllocator::Block *ProximityAllocator::find_existing_block(uint64_t addr,
    uint64_t distance, uint64_t size) {
  BlockMap::iterator iter = anchors_.begin();
  for (; iter != anchors_.end(); iter++) {
    Block *candidate = iter->second;
    if (candidate->can_provide(addr, distance, size))
      return candidate;
  }
  return NULL;
}

bool ProximityAllocator::Block::can_provide(uint64_t addr, uint64_t distance,
    uint64_t size) {
  return is_active_
      && is_within(addr, distance, next_)
      && is_within(addr, distance, next_ + size)
      && plus_saturate_max64(next_, size) < limit_;
}

tclib::Blob ProximityAllocator::Block::alloc(uint64_t size) {
  uint64_t start = next_;
  next_ += size;
  return tclib::Blob(reinterpret_cast<void*>(start), static_cast<size_t>(size));
}

bool ProximityAllocator::is_within(uint64_t base, uint64_t distance,
    uint64_t addr) {
  uint64_t half_distance = distance >> 1;
  uint64_t min = minus_saturate_zero(base, half_distance);
  uint64_t max = plus_saturate_max64(base, half_distance);
  return (min <= addr) && (addr < max);
}

bool ProximityAllocator::Block::ensure_capacity(uint64_t size, ProximityAllocator *owner) {
  if (!is_active_)
    // If this block is inactive we don't try ensuring again, we leave it dead.
    return false;
  // If there's room immediately we use that capacity.
  uint64_t remaining = limit_ - next_;
  if (remaining >= size)
    return true;
  // There's no room so we allocate a new block.
  tclib::Blob block;
  fat_bool_t succeeded = F_TRUE;
  if (is_restricted_) {
    succeeded = owner->direct_->alloc_executable(reinterpret_cast<address_t>(limit_),
        static_cast<size_t>(owner->block_size_), &block);
  } else {
    succeeded =  owner->direct_->alloc_executable(0,
        static_cast<size_t>(owner->block_size_), &block);
  }
  F_TRY_EXCEPT(succeeded);
    // We could get no memory so we deactivate this block such that we'll ignore
    // it from here on.
    is_active_ = false;
  F_YRT();
  next_ = reinterpret_cast<uint64_t>(block.start());
  limit_ = reinterpret_cast<uint64_t>(block.end());
  return true;
}

tclib::Blob ProximityAllocator::alloc_executable(address_t raw_addr,
    uint64_t distance, size_t raw_size) {
  uint64_t size = align_uint64(alignment_, raw_size);
  CHECK_REL("alloc too big", size, <=, block_size_);
  if (distance == 0) {
    // There are no restrictions on this allocation so try to use the
    // unrestricted allocator.
    Block *unrestricted = get_or_create_unrestricted();
    return unrestricted->ensure_capacity(size, this)
      ? unrestricted->alloc(size)
      : tclib::Blob();
  }
  uint64_t addr = reinterpret_cast<uint64_t>(raw_addr);
  // First attempt to get the memory from an existing block.
  Block *block = find_existing_block(addr, distance, size);
  if (block != NULL)
    return block->alloc(size);
  for (size_t ia = 0; ia < kAnchorCount; ia++) {
    uint32_t index = kAnchorOrder[ia];
    uint64_t anchor = get_anchor_from_address(addr, distance, index);
    Block *block = get_or_create_anchor(anchor);
    if (block->ensure_capacity(size, this) && block->can_provide(addr, distance, size))
      return block->alloc(size);
  }
  return tclib::Blob();
}

uint64_t ProximityAllocator::get_anchor_from_address(uint64_t addr,
    uint64_t distance, uint32_t index) {
  CHECK_REL("invalid anchor index", index, <=, kAnchorCount);
  if (distance == 0)
    // If there is no distance restriction we use the trivial bottom anchor.
    return 0;
  CHECK_TRUE("unaligned distance", is_power_of_2(distance));
  // There are kAnchorCount anchors within the distance and the alignment and
  // this is the alignment they snap to.
  uint64_t anchor_alignment = distance >> kLogAnchorCount;
  // The index relative to the middle of the range.
  int64_t relative_index = static_cast<int64_t>(index) - (kAnchorCount >> 1);
  // The distance to jump relative to the address.
  int64_t relative_offset = relative_index * anchor_alignment;
  // Use saturating ops to add the offset.
  uint64_t unaligned = (relative_offset < 0)
      ? minus_saturate_zero(addr, -relative_offset)
      : plus_saturate_max64(addr, relative_offset);
  // Finally align the result to get an anchor. The difference between aligning
  // before or after adding the offset is whether we align the saturation
  // boundaries or not; this way we do.
  return unaligned & ~(anchor_alignment - 1);
}

uint64_t ProximityAllocator::minus_saturate_zero(uint64_t a, uint64_t b) {
  return (a <= b) ? 0 : (a - b);
}

uint64_t ProximityAllocator::plus_saturate_max64(uint64_t a, uint64_t b) {
  return (a <= (-1ULL - b)) ? (a + b) : -1ULL;
}

ProximityAllocator::Block *ProximityAllocator::get_or_create_unrestricted() {
  if (unrestricted_ == NULL)
    unrestricted_ = new Block();
  return unrestricted_;
}

ProximityAllocator::Block *ProximityAllocator::get_or_create_anchor(uint64_t anchor) {
  anchor_key_t key = static_cast<anchor_key_t>(anchor);
  BlockMap::iterator existing = anchors_.find(key);
  if (existing != anchors_.end())
    return existing->second;
  Block *new_block = new Block(anchor);
  anchors_[key] = new_block;
  return new_block;
}

#ifdef IS_64_BIT
#  include "binpatch-x86-64.cc"
#else
#  include "binpatch-x86-32.cc"
#endif

#ifdef IS_MSVC
#include "binpatch-msvc.cc"
#else
#include "binpatch-posix.cc"
#endif
