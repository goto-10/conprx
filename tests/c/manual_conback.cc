//- Copyright 2016 the Neutrino authors (see AUTHORS).
//- Licensed under the Apache License, Version 2.0 (see LICENSE).

#include "test.hh"

#include "conback-utils.hh"

using namespace conprx;
using namespace tclib;

CONBACK_TEST(conback, read_console) {
  CONBACK_TEST_PREAMBLE();
  def_ref_t<ConsoleFrontend> native_frontend;
  def_ref_t<ConsolePlatform> native_platform;
  def_ref_t<WinTty> wty;
  if (!use_native) {
    native_frontend = ConsoleFrontend::new_native();
    native_platform = ConsolePlatform::new_native();
    wty = WinTty::new_adapted(*native_frontend, *native_platform);
    __multiplexer__.set_wty(*wty);
  }
  LOG_INFO("Type foo<enter>");
  handle_t std_in = platform->get_std_handle(kStdInputHandle);
  uint8_t buf[128];
  dword_t chars_read = 0;
  frontend->read_console_a(std_in, buf, 128, &chars_read, NULL);
  ASSERT_EQ(5, chars_read);
  ASSERT_BLOBEQ(tclib::Blob("foo\xd\xa", 5), tclib::Blob(buf, 5));
}
