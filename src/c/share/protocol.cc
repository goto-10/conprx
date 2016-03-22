//- Copyright 2016 the Neutrino authors (see AUTHORS).
//- Licensed under the Apache License, Version 2.0 (see LICENSE).

#include "share/protocol.hh"

#include "marshal-inl.hh"
#include "utils/callback.hh"

using namespace conprx;
using namespace plankton;
using namespace tclib;

DefaultSeedType<Handle> Handle::kSeedType("conprx.Handle");

Handle *Handle::new_instance(Variant header, Factory *factory) {
  return new (factory) Handle();
}

Variant Handle::to_seed(Factory *factory) {
  Seed result = factory->new_seed(seed_type());
  result.set_field("id", id_);
  return result;
}

void Handle::init(Seed payload, Factory *factory) {
  Variant id = payload.get_field("id");
  id_ = id.is_integer() ? id.integer_value() : kInvalidHandleValue;
}


NtStatus NtStatus::from(Severity severity, Origin origin, uint32_t code) {
  // The top part, 0xE000 means severity error, custom-defined, facility 0.
  // https://msdn.microsoft.com/en-us/library/cc231200.aspx
  return severity | origin | (code & kCodeMask);
}

dword_t NtStatus::decode_error(int32_t ntstatus) {
  return ((ntstatus & kSeverityMask) == kSeveritySuccess)
      ? 0
      : (ntstatus & kCodeMask);
}
