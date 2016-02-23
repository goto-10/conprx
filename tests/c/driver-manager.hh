//- Copyright 2016 the Neutrino authors (see AUTHORS).
//- Licensed under the Apache License, Version 2.0 (see LICENSE).

// The client side of the driver connection, that is, the process that talks to
// the driver -- the driver itself is the server.

#ifndef _CONPRX_DRIVER_MANAGER_HH
#define _CONPRX_DRIVER_MANAGER_HH

#include "agent/confront.hh"

#include "agent/launch.hh"
#include "async/promise-inl.hh"
#include "driver.hh"
#include "io/iop.hh"
#include "marshal-inl.hh"
#include "plankton-inl.hh"
#include "rpc.hh"
#include "sync/pipe.hh"
#include "sync/process.hh"
#include "sync/thread.hh"

namespace conprx {

// Using-declarations within headers is suspect for subtle reasons (google it
// for details) so just use the stuff we need.
using plankton::Variant;
using plankton::Factory;
using plankton::Arena;
using plankton::rpc::OutgoingRequest;
using plankton::rpc::IncomingResponse;
using plankton::rpc::StreamServiceConnector;

class DriverManager;

// An individual request to the driver.
class DriverRequest {
public:
  // All the different methods you can call through this request.
#define __DECL_PROXY_METHOD__(name, SIG, ARGS)                                 \
  Variant name SIG;
  FOR_EACH_REMOTE_MESSAGE(__DECL_PROXY_METHOD__)
#undef __DECL_PROXY_METHOD__

#define __DECL_PROXY_METHOD__(Name, name, FLAGS, SIG, PSIG)                    \
  Variant name PSIG(GET_SIG_PARAMS);
  FOR_EACH_CONAPI_FUNCTION(__DECL_PROXY_METHOD__)
#undef __DECL_PROXY_METHOD__

  // Returns a factory that will live as long as the request, so you can use it
  // to allocate data for sending with your request.
  Factory *factory() { return &arena_; }

  // Returns the resulting value of this request, assuming it succeeded. If it
  // is not yet resolved or failed this will fail.
  const Variant &operator*();

  // Returns the error resulting from this request, assuming it failed and
  // failing if it didn't.
  const Variant &error();

  // Returns a pointer to the value of this request, assuming it succeeded. If
  // not, same as *.
  const Variant *operator->() { return &this->operator*(); }

  bool is_rejected() { return response_->is_rejected(); }

private:
  friend class DriverManager;
  DriverRequest(DriverManager *connection)
    : is_used_(false)
    , manager_(connection) { }

  Variant send(Variant selector, Variant arg0);
  Variant send(Variant selector, Variant arg0, Variant arg1, Variant arg2);
  Variant send_request(OutgoingRequest *req);

  bool is_used_;
  DriverManager *manager_;
  IncomingResponse response_;
  Arena arena_;
};

// A launcher that knows how to start the driver and communicate with the fake
// launcher that the driver supports. It can't be used for anything other than
// testing.
class FakeAgentLauncher : public Launcher {
public:
  FakeAgentLauncher();
  virtual void default_destroy() { tclib::default_delete_concrete(this); }

  // Allocate the underlying channel.
  bool allocate();

  // We use this to not only connect the service but start the monitor thread
  // running.
  virtual bool connect_service();

  tclib::ServerChannel *agent_channel() { return *agent_channel_; }

protected:
  virtual tclib::InStream *owner_in() { return agent_channel()->in(); }
  virtual tclib::OutStream *owner_out() { return agent_channel()->out(); }

  // The fake agent launcher needs the process to be running before it can start
  // talking to the remote agent so this does almost all of the work, it
  // releases the process from being suspended and opens up the connection.
  virtual bool start_connect_to_agent();

  virtual bool use_agent() { return true; }

  // In addition to joining the process this also joins the agent monitor
  // thread.
  virtual bool join(int *exit_code_out);

private:
  // Main entry-point for the agent monitor thread.
  void *run_agent_monitor();

  tclib::def_ref_t<tclib::ServerChannel> agent_channel_;
  tclib::NativeThread agent_monitor_;
  tclib::Drawbridge agent_monitor_done_;
  tclib::Drawbridge *agent_monitor_done() { return &agent_monitor_done_; }
};

// Launcher that knows how to launch the driver with no agent. Useful for
// testing the plain driver.
class NoAgentLauncher : public Launcher {
public:
  virtual void default_destroy() { tclib::default_delete_concrete(this); }
  virtual bool use_agent() { return false; }

protected:
  // This is only present such that we can report an error if anyone tries to
  // call it.
  virtual bool start_connect_to_agent();

};

// A manager that manages the lifetime of the driver, including starting it up,
// and allows communication with it.
class DriverManager {
public:
  // Which kind of agent to use.
  enum AgentType {
    atNone,
    atFake,
    atReal
  };

  DriverManager();

  // Start up the driver executable.
  bool start();

  // Connect to the running driver.
  bool connect();

  // Wait for the driver to terminate.
  bool join();

  // When devutils are enabled, allows you to trace all the requests that go
  // through this manager.
  ONLY_ALLOW_DEVUTILS(void set_trace(bool value) { trace_ = value; })

  // Notify this manager that it should install the console agent. Once enabled
  // this can't be disabled.
  bool set_agent_type(AgentType type);

  // Returns a new request object that can be used to perform a single call to
  // the driver. The value returned by the call is only valid as long as the
  // request is.
  DriverRequest new_request() { return DriverRequest(this); }

  Launcher *operator->() { return launcher(); }

  // Constant that's true when the real agent can work.
  static const bool kSupportsRealAgent = kIsMsvc && !kIsDebugCodegen;

  // Shorthands for requests that don't need the request object before sending
  // the message.
#define __DECL_PROXY_METHOD__(name, SIG, ARGS)                                 \
  DriverRequest name SIG {                                                     \
    DriverRequest req(this);                                                   \
    req.name ARGS;                                                             \
    return req;                                                                \
  }
  FOR_EACH_REMOTE_MESSAGE(__DECL_PROXY_METHOD__)
#undef __DECL_PROXY_METHOD__

#define __DECL_PROXY_METHOD__(Name, name, FLAGS, SIG, PSIG)                    \
  DriverRequest name PSIG(GET_SIG_PARAMS) {                                    \
    DriverRequest req(this);                                                   \
    req.name PSIG(GET_SIG_ARGS);                                               \
    return req;                                                                \
  }
  FOR_EACH_CONAPI_FUNCTION(__DECL_PROXY_METHOD__)
#undef __DECL_PROXY_METHOD__

  IncomingResponse send(OutgoingRequest *req);

private:
  tclib::def_ref_t<Launcher> launcher_;
  Launcher *launcher() { return *launcher_; }

  // The channel through which we control the driver.
  tclib::def_ref_t<tclib::ServerChannel> channel_;
  tclib::ServerChannel *channel() { return *channel_;}
  tclib::def_ref_t<StreamServiceConnector> connector_;
  StreamServiceConnector *connector() { return *connector_; }

  bool trace_;
  AgentType agent_type_;
  AgentType agent_type() { return agent_type_; }
  static utf8_t executable_path();
  static utf8_t agent_path();
};

} // namespace conprx

#endif // _CONPRX_DRIVER_MANAGER_HH
