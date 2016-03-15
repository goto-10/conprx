//- Copyright 2014 the Neutrino authors (see AUTHORS).
//- Licensed under the Apache License, Version 2.0 (see LICENSE).

/// Console api implementation specific to the windows platform.

#include "confront.hh"
#include "conconn.hh"

BEGIN_C_INCLUDES
#include "utils/string-inl.h"
END_C_INCLUDES

using namespace conprx;
using namespace tclib;

// The fallback console is a console implementation that works on all platforms.
// The implementation is just placeholders but they should be functional enough
// that they can be used for testing.
class SimulatingConsoleFrontend : public ConsoleFrontend {
public:
  SimulatingConsoleFrontend(ConsoleAdaptor *adaptor, lpc::AddressXform xform);
  ~SimulatingConsoleFrontend();
  virtual void default_destroy() { tclib::default_delete_concrete(this); }

#define __DECLARE_CONAPI_METHOD__(Name, name, FLAGS, SIG, PSIG)                \
  virtual SIG(GET_SIG_RET) name SIG(GET_SIG_PARAMS);
  FOR_EACH_CONAPI_FUNCTION(__DECLARE_CONAPI_METHOD__)
#undef __DECLARE_CONAPI_METHOD__

  uint32_t get_console_cp(bool is_output);
  bool_t set_console_cp(uint32_t value, bool is_output);

  virtual dword_t get_last_error();

  virtual int64_t poke_backend(int64_t value);

  lpc::AddressXform xform() { return xform_; }

private:
  ConsoleAdaptor *adaptor() { return adaptor_; }
  ConsoleAdaptor *adaptor_;
  lpc::AddressXform xform_;
  dword_t last_error_;

};

SimulatingConsoleFrontend::SimulatingConsoleFrontend(ConsoleAdaptor *adaptor,
    lpc::AddressXform xform)
  : adaptor_(adaptor)
  , xform_(xform)
  , last_error_(0) { }

SimulatingConsoleFrontend::~SimulatingConsoleFrontend() {
}

int64_t SimulatingConsoleFrontend::poke_backend(int64_t value) {
  return adaptor()->poke(value).flush(0, &last_error_);
}

handle_t SimulatingConsoleFrontend::get_std_handle(dword_t n_std_handle) {
  return NULL;
}

bool_t SimulatingConsoleFrontend::write_console_a(handle_t console_output, const void *buffer,
    dword_t chars_to_write, dword_t *chars_written, void *reserved) {
  return false;
}

dword_t SimulatingConsoleFrontend::get_console_title_a(str_t str, dword_t n) {
  return 0;
}

// Convenience class for constructing simulated messages.
class SimulatedMessage {
public:
  SimulatedMessage(SimulatingConsoleFrontend *frontend);
  lpc::Message *message() { return &message_; }
  lpc::message_data_t *operator->() { return &data_; }
private:
  lpc::message_data_t data_;
  lpc::Message message_;
};

SimulatedMessage::SimulatedMessage(SimulatingConsoleFrontend *frontend)
  : message_(&data_, frontend->xform()) {
  struct_zero_fill(data_);
}

uint32_t SimulatingConsoleFrontend::get_console_cp(bool is_output) {
  SimulatedMessage message(this);
  lpc::get_console_cp_m *data = &message->payload.get_console_cp;
  data->is_output = is_output;
  adaptor()->get_console_cp(message.message(), data);
  last_error_ = message->return_value;
  return data->code_page_id;
}

uint32_t SimulatingConsoleFrontend::get_console_cp() {
  return get_console_cp(false);
}

uint32_t SimulatingConsoleFrontend::get_console_output_cp() {
  return get_console_cp(true);
}

bool_t SimulatingConsoleFrontend::set_console_cp(uint32_t value, bool is_output) {
  SimulatedMessage message(this);
  lpc::set_console_cp_m *data = &message->payload.get_console_cp;
  data->is_output = is_output;
  data->code_page_id = value;
  adaptor()->set_console_cp(message.message(), data);
  last_error_ = message->return_value;
  return true;
}

bool_t SimulatingConsoleFrontend::set_console_cp(uint32_t value) {
  return set_console_cp(value, false);
}


bool_t SimulatingConsoleFrontend::set_console_output_cp(uint32_t value) {
  return set_console_cp(value, true);
}

bool_t SimulatingConsoleFrontend::set_console_title_a(ansi_cstr_t str) {
  SimulatedMessage message(this);
  lpc::set_console_title_m *data = &message->payload.set_console_title;
  size_t length = strlen(str);
  data->length = length;
  data->title = xform().local_to_remote(const_cast<ansi_str_t>(str));
  adaptor()->set_console_title(message.message(), data);
  last_error_ = message->return_value;
  return true;
}

dword_t SimulatingConsoleFrontend::get_last_error() {
  return last_error_;
}

pass_def_ref_t<ConsoleFrontend> ConsoleFrontend::new_simulating(ConsoleAdaptor *adaptor,
    ssize_t delta) {
  return pass_def_ref_t<ConsoleFrontend>(new (kDefaultAlloc) SimulatingConsoleFrontend(adaptor,
      lpc::AddressXform(delta)));
}
