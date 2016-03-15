//- Copyright 2014 the Neutrino authors (see AUTHORS).
//- Licensed under the Apache License, Version 2.0 (see LICENSE).

#include "agent.hh"
#include "agent/conconn.hh"
#include "async/promise-inl.hh"
#include "binpatch.hh"
#include "confront.hh"
#include "marshal-inl.hh"
#include "utils/log.hh"

BEGIN_C_INCLUDES
#include "utils/string-inl.h"
END_C_INCLUDES

using namespace conprx;
using namespace plankton;
using namespace tclib;

LogEntry::LogEntry() {
  log_entry_default_init(&entry_, llInfo, NULL, 0, string_empty(), string_empty());
}

DefaultSeedType<LogEntry> LogEntry::kSeedType("conprx.LogEntry");

LogEntry *LogEntry::new_instance(Variant header, Factory *factory) {
  return new (factory) LogEntry();
}

Variant LogEntry::to_seed(Factory *factory) {
  Seed seed = factory->new_seed(seed_type());
  seed.set_field("level", entry_.level);
  seed.set_field("file", entry_.file);
  seed.set_field("line", entry_.line);
  seed.set_field("message", entry_.message.chars);
  seed.set_field("timestamp", entry_.timestamp.chars);
  return seed;
}

void LogEntry::init(plankton::Seed payload, plankton::Factory *factory) {
  log_level_t level = static_cast<log_level_t>(payload.get_field("level").integer_value());
  const char *file = payload.get_field("file").string_chars();
  uint32_t line = static_cast<uint32_t>(payload.get_field("line").integer_value());
  const char *message = payload.get_field("message").string_chars();
  const char *timestamp = payload.get_field("timestamp").string_chars();
  log_entry_default_init(&entry_, level, file, line, new_c_string(message),
      new_c_string(timestamp));
}

bool StreamingLog::record(log_entry_t *entry) {
  rpc::OutgoingRequest req(Variant::null(), "log");
  LogEntry entry_data(entry);
  Native entry_var = req.factory()->new_native(&entry_data);
  req.set_arguments(1, &entry_var);
  rpc::IncomingResponse resp = out_->socket()->send_request(&req);
  while (!resp->is_settled())
    out_->input()->process_next_instruction(NULL);
  return propagate(entry);
}

ConsoleAgent::ConsoleAgent()
  : agent_in_(NULL)
  , agent_out_(NULL) { }

const char *ConsoleAgent::get_lpc_name(ulong_t number) {
  switch (number) {
#define __GEN_CASE__(Name, name, DLL, API) case ((DLL << 16) | API): return #Name;
  FOR_EACH_LPC_TO_INTERCEPT(__GEN_CASE__)
  FOR_EACH_OTHER_KNOWN_LPC(__GEN_CASE__)
#undef __GEN_CASE__
  default:
    return NULL;
  }
}

fat_bool_t ConsoleAgent::on_message(lpc::Interceptor *interceptor,
    lpc::Message *request, lpc::message_data_t *incoming_reply) {
  switch (request->api_number()) {
    // The messages we want to handle.
#define __EMIT_CASE__(Name, name, DLL, API)                                    \
    case lm##Name:                                                             \
      return on_##name(request, &request->data()->payload.name);
  FOR_EACH_LPC_TO_INTERCEPT(__EMIT_CASE__)
#undef __EMIT_CASE__
    // The messages we know about but don't want to handle.
#define __EMIT_CASE__(Name, name, DLL, API)                                    \
    case CALC_API_NUMBER(DLL, API):
      return F_FALSE;
  FOR_EACH_OTHER_KNOWN_LPC(__EMIT_CASE__)
#undef __EMIT_CASE__
    // Unknown messages.
    default: {
      if (kDumpUnknownMessages) {
        lpc::Interceptor::Disable disable(interceptor);
        request->dump(FileSystem::native()->std_out());
      }
      return F_FALSE;
    }
  }
}

fat_bool_t ConsoleAgent::on_get_console_cp(lpc::Message *req, lpc::get_console_cp_m *data) {
  Response<uint32_t> resp = connector()->get_console_cp(data->is_output);
  req->data()->return_value = static_cast<ulong_t>(resp.error());
  data->code_page_id = (resp.has_error() ? 0 : resp.value());
  return F_TRUE;
}

fat_bool_t ConsoleAgent::on_set_console_cp(lpc::Message *req, lpc::set_console_cp_m *data) {
  Response<bool_t> resp = connector()->set_console_cp(
      static_cast<uint32_t>(data->code_page_id), data->is_output);
  req->data()->return_value = static_cast<ulong_t>(resp.error());
  return F_TRUE;
}

fat_bool_t ConsoleAgent::on_set_console_title(lpc::Message *req, lpc::set_console_title_m *data) {
  void *start = req->translate(data->title);
  tclib::Blob blob(start, data->length);
  Response<bool_t> resp = connector()->set_console_title(blob, data->is_unicode);
  req->data()->return_value = static_cast<ulong_t>(resp.error());
  return F_TRUE;
}

fat_bool_t ConsoleAgent::install_agent(tclib::InStream *agent_in,
    tclib::OutStream *agent_out) {
  agent_in_ = agent_in;
  agent_out_ = agent_out;
  owner_ = new (kDefaultAlloc) rpc::StreamServiceConnector(agent_in, agent_out);
  if (!owner()->init(empty_callback()))
    return F_FALSE;
  log()->set_destination(owner());
  log()->ensure_installed();
  F_TRY(install_agent_platform());
  send_is_ready();
  return F_TRUE;
}

fat_bool_t ConsoleAgent::uninstall_agent() {
  send_is_done();
  F_TRY(uninstall_agent_platform());
  log()->ensure_uninstalled();
  if (agent_in_ != NULL)
    F_TRY(F_BOOL(agent_in_->close()));
  if (agent_out_ != NULL)
    F_TRY(F_BOOL(agent_out_->close()));
  return F_TRUE;
}

fat_bool_t ConsoleAgent::send_is_ready() {
  rpc::OutgoingRequest req(Variant::null(), "is_ready", 0, NULL);
  rpc::IncomingResponse resp;
  return send_request(&req, &resp);
}

fat_bool_t ConsoleAgent::send_is_done() {
  rpc::OutgoingRequest req(Variant::null(), "is_done", 0, NULL);
  rpc::IncomingResponse resp;
  return send_request(&req, &resp);
}

fat_bool_t ConsoleAgent::send_request(rpc::OutgoingRequest *request,
    rpc::IncomingResponse *resp_out) {
  rpc::IncomingResponse resp = owner()->socket()->send_request(request);
  while (!resp->is_settled())
    F_TRY(owner()->input()->process_next_instruction(NULL));
  *resp_out = resp;
  return F_TRUE;
}

#ifdef IS_MSVC
#include "agent-msvc.cc"
#endif
