//- Copyright 2014 the Neutrino authors (see AUTHORS).
//- Licensed under the Apache License, Version 2.0 (see LICENSE).

/// This is a wrapper around the LLVM disassembler code. The goal is to keep
/// changes to that code minimal so to the extent possible we effect changes by
/// defining stuff here and then source-including their source.

#include "utils/log.hh"
#include "disassembler-x86.hh"

#define __STDC_LIMIT_MACROS
#define __STDC_CONSTANT_MACROS
#define NDEBUG 1

// These have been hoisted from the LLVM file.
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MemoryObject.h"
#include "llvm/Support/TargetRegistry.h"

namespace llvm {

// Stub disassembler supertype.
class MCDisassembler {
public:
  virtual ~MCDisassembler() { }
  MCDisassembler(const llvm::MCSubtargetInfo &) : CommentStream(NULL) { }
  enum DecodeStatus {
    Fail = 0,
    SoftFail = 1,
    Success = 3
  };

  bool tryAddingSymbolicOperand(MCInst &Inst,
                                int64_t Value,
                                uint64_t Address, bool IsBranch,
                                uint64_t Offset, uint64_t InstSize) const {
    return true;
  }

  void tryAddingPcLoadReferenceComment(int64_t Value, uint64_t Address) const {

  }

  virtual DecodeStatus  getInstruction(MCInst& instr,
                                       uint64_t& size,
                                       const MemoryObject &region,
                                       uint64_t address,
                                       raw_ostream &vStream,
                                       raw_ostream &cStream) const = 0;

  mutable raw_ostream *CommentStream;
};

}

#include "X86Disassembler.h"
#include "X86Disassembler.cc"

using namespace conprx;

// An llvm memory object that reads directly from a byte vector.
class VectorMemory : public llvm::MemoryObject {
public:
  VectorMemory(Vector<byte_t> memory) : memory_(memory) { }
  virtual uint64_t getBase() const { return reinterpret_cast<uint64_t>(memory_.start()); }
  virtual uint64_t getExtent() const { return memory_.length(); }
  virtual int readByte(uint64_t address, uint8_t *ptr) const {
    if (memory_.length() <= address) {
      return -1;
    } else {
      *ptr = memory_[address];
      return 0;
    }
  }
private:
  Vector<byte_t> memory_;
};

Disassembler::~Disassembler() { }

// An x86-64 disassembler implemented by calling through to the llvm
// disassembler.
class Dis_X86_64 : public Disassembler {
public:
  virtual size_t get_instruction_length(Vector<byte_t> code);
};

Disassembler &Disassembler::x86_64() {
  static Dis_X86_64 *instance = NULL;
  if (instance == NULL)
    instance = new Dis_X86_64();
  return *instance;
}

size_t Dis_X86_64::get_instruction_length(Vector<byte_t> code) {
  LLVMInitializeX86Disassembler();
  llvm::MCSubtargetInfo *subtarget = llvm::TheX86_64Target.createMCSubtargetInfo("", "", "");
  llvm::MCDisassembler *disass = llvm::TheX86_64Target.createMCDisassembler(*subtarget);
  llvm::MCInst instr;
  uint64_t size = 0;
  VectorMemory memory(code);
  disass->getInstruction(instr, size, memory, 0, llvm::nulls(), llvm::nulls());
  return size;
}