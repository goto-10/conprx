//- Copyright 2014 the Neutrino authors (see AUTHORS).
//- Licensed under the Apache License, Version 2.0 (see LICENSE).

/// Console api implementation specific to the windows platform.

#include "agent/confront.hh"

using namespace conprx;
using namespace tclib;

class WindowsConsoleFrontend : public ConsoleFrontend {
public:
  virtual void default_destroy() { default_delete_concrete(this); }

#define __DECLARE_CONAPI_METHOD__(Name, name, FLAGS, SIG, PSIG)                \
  mfUnlessPf(FLAGS, virtual SIG(GET_SIG_RET) name SIG(GET_SIG_PARAMS));
  FOR_EACH_CONAPI_FUNCTION(__DECLARE_CONAPI_METHOD__)
#undef __DECLARE_CONAPI_METHOD__

  int64_t WindowsConsoleFrontend::poke_backend(int64_t value);

  virtual NtStatus get_last_error();
};

int64_t WindowsConsoleFrontend::poke_backend(int64_t value) {
  if (value > 0) {
    return value;
  } else {
    SetLastError(-value);
    return 0;
  }
}

bool_t WindowsConsoleFrontend::write_console_a(handle_t output, const void *buffer,
    dword_t chars_to_write, dword_t *chars_written, void *reserved) {
  return WriteConsoleA(output, buffer, chars_to_write, chars_written, reserved);
}

bool_t WindowsConsoleFrontend::write_console_w(handle_t output, const void *buffer,
    dword_t chars_to_write, dword_t *chars_written, void *reserved) {
  return WriteConsoleW(output, buffer, chars_to_write, chars_written, reserved);
}

bool_t WindowsConsoleFrontend::read_console_a(handle_t input, void *buffer,
    dword_t chars_to_read, dword_t *chars_read, console_readconsole_control_t *input_control) {
  return ReadConsoleA(input, buffer, chars_to_read, chars_read, input_control);
}

bool_t WindowsConsoleFrontend::read_console_w(handle_t input, void *buffer,
    dword_t chars_to_read, dword_t *chars_read, console_readconsole_control_t *input_control) {
  return ReadConsoleW(input, buffer, chars_to_read, chars_read, input_control);
}

dword_t WindowsConsoleFrontend::get_console_title_a(ansi_str_t str, dword_t n) {
  return GetConsoleTitleA(str, n);
}

bool_t WindowsConsoleFrontend::set_console_title_a(ansi_cstr_t str) {
  return SetConsoleTitleA(str);
}

dword_t WindowsConsoleFrontend::get_console_title_w(wide_str_t str, dword_t n) {
  return GetConsoleTitleW(str, n);
}

bool_t WindowsConsoleFrontend::set_console_title_w(wide_cstr_t str) {
  return SetConsoleTitleW(str);
}

uint32_t WindowsConsoleFrontend::get_console_cp() {
  return GetConsoleCP();
}

bool_t WindowsConsoleFrontend::set_console_cp(uint32_t value) {
  return SetConsoleCP(value);
}

bool_t WindowsConsoleFrontend::set_console_cursor_position(handle_t output,
    coord_t position) {
  return SetConsoleCursorPosition(output, position);
}

uint32_t WindowsConsoleFrontend::get_console_output_cp() {
  return GetConsoleOutputCP();
}

bool_t WindowsConsoleFrontend::set_console_output_cp(uint32_t value) {
  return SetConsoleOutputCP(value);
}

bool_t WindowsConsoleFrontend::get_console_mode(handle_t handle, dword_t *mode_out) {
  return GetConsoleMode(handle, mode_out);
}

bool_t WindowsConsoleFrontend::set_console_mode(handle_t handle, dword_t mode) {
  return SetConsoleMode(handle, mode);
}

bool_t WindowsConsoleFrontend::get_console_screen_buffer_info(handle_t handle,
    console_screen_buffer_info_t *info_out) {
  return GetConsoleScreenBufferInfo(handle, info_out);
}

bool_t WindowsConsoleFrontend::get_console_screen_buffer_info_ex(handle_t handle,
    console_screen_buffer_infoex_t *info_out) {
  return GetConsoleScreenBufferInfoEx(handle, info_out);
}

NtStatus WindowsConsoleFrontend::get_last_error() {
  return NtStatus::from_nt(GetLastError());
}

pass_def_ref_t<ConsoleFrontend> ConsoleFrontend::new_native() {
  return new (kDefaultAlloc) WindowsConsoleFrontend();
}

class WindowsConsolePlatform : public ConsolePlatform {
public:
  virtual void default_destroy() { default_delete_concrete(this); }

#define __DECLARE_PLATFORM_METHOD__(Name, name, FLAGS, SIG, PSIG)              \
  mfOnlyPf(FLAGS, virtual SIG(GET_SIG_RET) name SIG(GET_SIG_PARAMS));
  FOR_EACH_CONAPI_FUNCTION(__DECLARE_PLATFORM_METHOD__)
#undef __DECLARE_PLATFORM_METHOD__
};

handle_t WindowsConsolePlatform::get_std_handle(dword_t id) {
  return GetStdHandle(id);
}

fat_bool_t WindowsConsolePlatform::create_process(utf8_t executable, size_t argc,
    utf8_t *argv, pass_def_ref_t<NativeProcess> *process_out) {
  return create_native_process(executable, argc, argv, process_out);
}

pass_def_ref_t<ConsolePlatform> ConsolePlatform::new_native() {
  return new (kDefaultAlloc) WindowsConsolePlatform();
}
