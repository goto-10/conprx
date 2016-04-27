//- Copyright 2014 the Neutrino authors (see AUTHORS).
//- Licensed under the Apache License, Version 2.0 (see LICENSE).

#include "host.hh"
#include "launch.hh"

BEGIN_C_INCLUDES
#include "utils/string-inl.h"
#include "utils/log.h"
END_C_INCLUDES

using namespace tclib;
using namespace conprx;

static bool should_trace() {
  const char *envval = getenv("TRACE_CONPRX_HOST");
  return (envval != NULL) && (strcmp(envval, "1") == 0);
}

fat_bool_t fat_main(int argc, char *argv[], int *exit_code_out) {
  if (argc < 3) {
    fprintf(stderr, "Usage: host <library> <command ...>\n");
    return F_FALSE;
  }
  utf8_t library = new_c_string(argv[1]);
  utf8_t command = new_c_string(argv[2]);

  int skip_count = 3;
  int new_argc = argc - skip_count;
  utf8_t *new_argv = new utf8_t[new_argc];
  for (int i = 0; i < new_argc; i++)
    new_argv[i] = new_c_string(argv[skip_count + i]);

  def_ref_t<ConsoleFrontend> own_frontend = ConsoleFrontend::new_native();
  def_ref_t<ConsolePlatform> own_platform = ConsolePlatform::new_native();
  def_ref_t<WinTty> wty = WinTty::new_adapted(*own_frontend, *own_platform);
  BasicConsoleBackend backend;
  backend.set_wty(*wty);
  backend.set_title("Console host");
  InjectingLauncher launcher(library);
  launcher.set_backend(&backend);

  F_TRY(launcher.initialize());
  F_TRY(launcher.start(command, new_argc, new_argv));

  plankton::rpc::TracingMessageSocketObserver observer;
  if (should_trace())
    observer.install(launcher.socket());

  F_TRY(launcher.process_messages());
  F_TRY(launcher.join(exit_code_out));

  return F_TRUE;
}

int main(int argc, char *argv[]) {
  int exit_code = 0;
  fat_bool_t result = fat_main(argc, argv, &exit_code);
  if (result) {
    return exit_code;
  } else {
    LOG_ERROR("Failed to launch process at " kFatBoolFileLine,
        fat_bool_file(result), fat_bool_line(result));
    return (exit_code == 0) ? 1 : exit_code;
  }
}
