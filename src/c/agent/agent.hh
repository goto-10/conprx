//- Copyright 2014 the Neutrino authors (see AUTHORS).
//- Licensed under the Apache License, Version 2.0 (see LICENSE).

/// # Agent
///
/// The console agent which is the part of the console proxy infrastructure that
/// translates an applications's use of the windows console api to messages with
/// the console handler.
///
/// The agent is injected into all processes that are covered by the console
/// proxy infrastructure, which it typically all of them, so it is constrained
/// by a number of factors:
///
///    * It must be secure. Any security issued introduced by the agent will
///      affect all processes would be a Bad Thing(TM).
///    * The injection process should be fast since it runs on every process
///      creation.
///    * It should be small since it will take up space in all processes.
///
/// The agent consists of a number of components spread among a number of files.
///
///    * The agent itself which is declared in this file is responsible for
///      initiating the injection process.
///    * The {{binpatch.hh}}(binary patching infrastructure) does the actual
///      platform-specific code patching.
///    * The {{conapi.hh}}(console api) is a framework for implementing
///      alternative consoles. The agent can have different functions
///      (delegating, tracing, etc.) depending on which replacement console api
///      is selected.
///
/// In addition to these core components there are components used for testing
/// and debugging,
///
///    * The {{host.hh}}(console host) is a command-line utility that runs a
///      command with the agent injected.
///
/// ## Options
///
/// The agent understands a number of environment options, set either through
/// the registry or as environment variables. On startup options are read in
/// sequence from
///
///    1. `HKEY_LOCAL_MACHINE\Software\Tundra\Console Agent\...`
///    2. `HKEY_CURRENT_USER\Software\Tundra\Console Agent\...`
///    3. Environment variables.
///
/// Later values override earlier ones, so the local machine settings can
/// override the defaults, the user settings can override machine settings, and
/// environment settings can override user settings.
///
/// The naming convention for options are upper camel case for registry entries,
/// all caps for environment variables. The options are,
///
///    * `Enabled`/`CONSOLE_AGENT_ENABLED`: whether to patch code or just bail
///      out immediately. The default is true.
///    * `VerboseLogging`/`CONSOLE_AGENT_VERBOSE_LOGGING`: log what the agent
///      does, both successfully and on failures. The default is to log only
///      on failures.
///
/// Setting a registry option to integer `0` disables the option, `1` enables
/// it. Setting an environment variable to the string `"0"` disables an option,
/// `"1"` enables it.
///
/// ## Blacklist
///
/// The agent has a blacklist of processes it refuses to patch. In some cases
/// they're core services which we shouldn't tamper with, in other cases they're
/// fallbacks that we want to be sure work even if the agent misbehaves. For
/// instance, the registry editor is blacklisted because if the agent misbehaves
/// we may need to disable it by setting registry values.

#ifndef _CONPRX_AGENT_AGENT_HH
#define _CONPRX_AGENT_AGENT_HH

#include "binpatch.hh"
#include "confront.hh"
#include "io/stream.hh"
#include "lpc.hh"
#include "rpc.hh"
#include "utils/fatbool.hh"
#include "utils/log.hh"
#include "utils/types.hh"

namespace conprx {

using plankton::rpc::StreamServiceConnector;

class Options;

// Log that streams messages onto an output stream before passing log handling
// on to the enclosing log.
class StreamingLog : public tclib::Log {
public:
  StreamingLog() : out_(NULL) { }
  virtual fat_bool_t record(log_entry_t *entry);
  void set_destination(StreamServiceConnector *out) { out_ = out; }

private:
  StreamServiceConnector *out_;
};

// Controls the injection of the console agent.
class ConsoleAgent : public tclib::DefaultDestructable {
public:
  ConsoleAgent();
  virtual ~ConsoleAgent() { }

  static const int32_t kConnectDataMagic = 0xFABACAEA;

  static const int cInvalidConnectDataSize = 0x11110000;
  static const int cInvalidConnectDataMagic = 0x11120000;
  static const int cInstallationFailed = 0x11130000;
  static const int cFailedToOpenParentProcess = 0x11140000;
  static const int cFailedToDuplicateOwnerIn = 0x11150000;
  static const int cFailedToDuplicateOwnerOut = 0x11160000;
  static const int cSuccess = 0x0;

  // Install this agent.
  fat_bool_t install_agent(tclib::InStream *agent_in, tclib::OutStream *agent_out,
      ConsolePlatform *platform);

  // Remove this agent.
  fat_bool_t uninstall_agent();

  // Send a request back to the owner and wait for a response.
  fat_bool_t send_request(plankton::rpc::OutgoingRequest *request,
      plankton::rpc::IncomingResponse *response_out);

  // If the given number represents an lpc we know about returns the string name
  // of it. Otherwise return NULL.
  static const char *get_lpc_name(ulong_t number);

  // Processes LPC messages. The return value is used to decide whether to
  // report an error or not.
  NtStatus on_message(lpc::Message *request);

  StreamServiceConnector *owner() { return *owner_; }

  virtual ConsoleAdaptor *adaptor() { return NULL; }

  enum lpc_method_key_t {
    lmFirst
#define __GEN_KEY_ENUM__(Name, name, NUM, FLAGS) , lm##Name = (NUM)
    FOR_EACH_LPC_TO_INTERCEPT(__GEN_KEY_ENUM__)
#undef __GEN_KEY_ENUM__
  };

protected:
  // Perform the platform-specific part of the agent installation.
  virtual fat_bool_t install_agent_platform() = 0;

  // Perform the platform-specific part of the agent uninstall.
  virtual fat_bool_t uninstall_agent_platform() = 0;

private:
  // Send the is-ready message to the owner.
  fat_bool_t send_is_ready();

  fat_bool_t send_is_done();

  // Tracer methods that dump the contents of the message.
  void trace_before(const char *name, lpc::Message *message);
  void trace_after(const char *name, lpc::Message *message, NtStatus status);

  // A connection to the owner of the agent.
  tclib::def_ref_t<StreamServiceConnector> owner_;
  tclib::InStream *agent_in_;
  tclib::OutStream *agent_out_;

  ConsolePlatform *platform() { return platform_; }
  ConsolePlatform *platform_;

  StreamingLog *log() { return &log_; }
  StreamingLog log_;
};

} // namespace conprx

#endif // _CONPRX_AGENT_AGENT_HH
