//- Copyright 2016 the Neutrino authors (see AUTHORS).
//- Licensed under the Apache License, Version 2.0 (see LICENSE).

#include "conback-utils.hh"

using namespace conprx;
using namespace plankton;
using namespace tclib;

SimulatedFrontendAdaptor::SimulatedFrontendAdaptor(ConsoleBackend *backend)
  : backend_(backend)
  , buffer_(1024)
  , streams_(&buffer_, &buffer_)
  , connector_(streams_.socket(), streams_.input())
  , agent_(&connector_)
  , service_(NULL)
  , trace_(false)
  , tracer_("SB") {
  streams_.set_default_type_registry(ConsoleTypes::registry());
  platform_ = InMemoryConsolePlatform::new_simulating(&agent_);
  frontend_ = ConsoleFrontend::new_simulating(&agent_, *platform_);
  service_.set_backend(backend_);
}

fat_bool_t SimulatedFrontendAdaptor::initialize() {
  if (trace_)
    tracer_.install(streams_.socket());
  if (!buffer_.initialize())
    return F_FALSE;
  F_TRY(streams_.init(service_.handler()));
  return F_TRUE;
}

fat_bool_t FrontendMultiplexer::initialize() {
  if (use_native_) {
    native_frontend_ = ConsoleFrontend::new_native();
    frontend_ = *native_frontend_;
    native_platform_ = ConsolePlatform::new_native();
    platform_ = *native_platform_;
  } else {
    fake_frontend_ = new (kDefaultAlloc) SimulatedFrontendAdaptor(&backend_);
    fake_frontend_->set_trace(trace_);
    F_TRY(fake_frontend_->initialize());
    frontend_ = fake_frontend_->frontend();
    platform_ = fake_frontend_->platform();
  }
  return F_TRUE;
}
