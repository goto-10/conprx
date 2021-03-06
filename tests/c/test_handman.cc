//- Copyright 2016 the Neutrino authors (see AUTHORS).
//- Licensed under the Apache License, Version 2.0 (see LICENSE).

#include "server/handman.hh"
#include "test.hh"

using namespace conprx;
using namespace tclib;

TEST(handman, get_set) {
  HandleManager manager;
  Handle a(1);
  ASSERT_TRUE(manager.get_or_create_shadow(a, false) == NULL);
  {
    HandleShadow *a_info = manager.get_or_create_shadow(a, true);
    ASSERT_EQ(0, a_info->mode());
    a_info->set_mode(10);
  }
  ASSERT_EQ(10, manager.get_or_create_shadow(a, false)->mode());
  ASSERT_EQ(10, manager.get_or_create_shadow(a, true)->mode());
  Handle b(2);
  ASSERT_TRUE(manager.get_or_create_shadow(b, false) == NULL);
  {
    HandleShadow *b_info = manager.get_or_create_shadow(b, true);
    ASSERT_EQ(0, b_info->mode());
    b_info->set_mode(11);
  }
  ASSERT_EQ(10, manager.get_or_create_shadow(a, false)->mode());
  ASSERT_EQ(10, manager.get_or_create_shadow(a, true)->mode());
  ASSERT_EQ(11, manager.get_or_create_shadow(b, false)->mode());
  ASSERT_EQ(11, manager.get_or_create_shadow(b, true)->mode());
}
