//- Copyright 2014 the Neutrino authors (see AUTHORS).
//- Licensed under the Apache License, Version 2.0 (see LICENSE).

// Type declarations used by the console api.

#ifndef _AGENT_LPC_HH
#define _AGENT_LPC_HH

#include "agent/binpatch.hh"
#include "agent/conapi-types.hh"
#include "c/stdc.h"
#include "io/stream.hh"
#include "share/protocol.hh"
#include "utils/callback.hh"

// List all the lpc functions to patch. Note that the calling conventions are
// important here -- if you find your code crashing immediately after calls to
// these or their imposters (including stack cookie checks) you may want to look
// into whether it's because they're declared with the wrong calling convention.
// In particular, if crashes only happen on fully-optimized 32-bit windows and
// it turns out ESP ends up with an incorrect value, this is a good candidate
// for what the cause it. If you can call into the function in question you can
// tell its calling convention by how the name is mangled.
#define FOR_EACH_LPC_FUNCTION(F)                                               \
  F(NtRequestWaitReplyPort, nt_request_wait_reply_port, ntstatus_t,            \
     (handle_t port_handle, lpc::message_data_t *request, lpc::message_data_t *incoming_reply), \
     (port_handle, request, incoming_reply))

namespace lpc {

struct message_data_t;
struct capture_buffer_data_t;
class Message;

// A tranformation that turns local addresses into remote ones (or back) based
// on a fixed delta.
class AddressXform {
public:
  AddressXform(ssize_t delta = 0) : delta_(delta) { }

  // Given a pointer in the remote address space, returns it in the local. Null
  // pointers are left null.
  template <typename T> T *remote_to_local(T *arg);

  // Given a pointer in the local address space, returns it in the remote. Null
  // pointers are left null.
  template <typename T> T *local_to_remote(T *arg);

private:
  ssize_t delta_;
};

template <typename T>
T *AddressXform::remote_to_local(T *arg) {
  return (arg == NULL)
      ? NULL
      : reinterpret_cast<T*>(reinterpret_cast<address_arith_t>(arg) + delta_);
}

template <typename T>
T *AddressXform::local_to_remote(T *arg) {
  return (arg == NULL)
      ? NULL
      : reinterpret_cast<T*>(reinterpret_cast<address_arith_t>(arg) - delta_);
}

// The amount of stack to look at to try to determine the location of
// ConsoleClientCallServer. It's 4 because the stack is expected to look like
// this,
//
//   - Interceptor::nt_request_wait_reply_port_bridge
//   - ConsoleClientCallServer
//   - GetConsoleCP
//   - Interceptor::calibrate
//   - ...
//
// so 4 frames lets us validate that it does.
#define kLocateCCCSStackTemplateSize 4

// The amount of stack to capture and compare against the template.
#define kLocateCCCSStackCaptureSize 5

// An interceptor deals with the full process of replacing the implementation of
// NtRequestWaitReplyPort. There's a few steps to getting everything set up
// properly and an interceptor deals with all that.
class Interceptor {
public:
  typedef tclib::callback_t<conprx::NtStatus(lpc::Message*)> handler_t;

  // Creates an interceptor that will delegate intercepted calls to the given
  // handler.
  Interceptor(handler_t handler);

  // Installs the interceptor.
  fat_bool_t install();

  // Restores everything to the way it was before the interceptor was installed.
  fat_bool_t uninstall();

  // Disables redirection for the lifetime of instances, restoring the previous
  // value on destruction.
  class Disable {
  public:
    Disable(Interceptor* interceptor);
    ~Disable();
  private:
    Interceptor *interceptor_;
    bool was_enabled_;
  };

  // Try to infer the address of a function given an array of function pointers
  // that we expect will be present in a stack trace, as well as a stack trace.
  // The function array indicates where the pcs in the stack trace must be in
  // order for the inference to try to extract the address. One of the blobs
  // must have a NULL start which indicates that it's a placeholder for the
  // function we're interested in. On success a pointer to the beginning of the
  // function is stored to result_out, otherwise false is returned.
  static fat_bool_t infer_address_guided(Vector<tclib::Blob> functions,
      Vector<void*> stack_trace, void **result_out);

  // Given a function that is assumed to call a function we're interested in,
  // scan over the code of the function and return the address of that function.
  // Only returns true if it's fairly sure it's found the call, if if returns
  // false the state of the out parameter is undefined. If return_first is true
  // it will be less strict and just return the first call, otherwise it will
  // try to ensure that it's the only 32-bit relative call.
  static fat_bool_t infer_address_from_caller(tclib::Blob function,
      void **result_out, bool return_first);

  // Capture the current stack trace, storing it in the given buffer. Returns
  // true iff enough stack could be captured to fill the buffer.
  static fat_bool_t capture_stacktrace(Vector<void*> buffer);

private:
  friend class Message;

  // The type of ConsoleClientCallServer. Should really be called
  // console_client_call_server_f but that's just sooo verbose.
  typedef ntstatus_t (MSVC_STDCALL *cccs_f)(void *message, void *capture_buffer,
      ulong_t api_number, ulong_t request_length);

  // Sends the calibration message through the built-in message system to set
  // the internal state of the console properly. This must be a static function;
  // see why in process_locate_cccs_message.
  static fat_bool_t calibrate(Interceptor *interceptor);

  // Does the remaining work of calibrate after it no longer needs to be static.
  fat_bool_t infer_calibration_from_cccs();

  // Processes a stack trace to extract the address of ConsoleClientCallServer,
  // if possible.
  fat_bool_t process_locate_cccs_message(handle_t port_handle,
      Vector<void*>);

  // Returns -1 if the stack grows down (it almost surely does) and 1 if it
  // grows up.
  int32_t infer_stack_direction();

  // Process the calibration message sent after we've located CCCS.
  fat_bool_t process_calibration_message(handle_t port_handle, message_data_t *message);

  // Initialize the given patch.
  fat_bool_t initialize_patch(conprx::PatchRequest *request, const char *name,
      address_t replacement, module_t ntdll);

  // Store the patch requests together in an array.
#define __ADD_ONE__(Name, name, RET, SIG, ARGS) + 1
  static const size_t kPatchRequestCount = 0 FOR_EACH_LPC_FUNCTION(__ADD_ONE__);
  conprx::PatchRequest patch_requests_[0 FOR_EACH_LPC_FUNCTION(__ADD_ONE__)];
#undef __ADD_ONE__

  // This enum is mainly there to produce an index for each patch request that
  // can be used to get them from the array.
  enum patch_request_key_t {
    prFirst = -1
#define __DECLARE_REQUEST_KEY__(Name, name, RET, SIG, ARGS) , pr##Name
    FOR_EACH_LPC_FUNCTION(__DECLARE_REQUEST_KEY__)
#undef __DECLARE_REQUEST_KEY__
  };

  // Declare the state and functions used to interact with each function to
  // patch.
#define __DECLARE_REPLACEMENT_FUNCTION__(Name, name, RET, SIG, ARGS)           \
  typedef RET (MSVC_STDCALL *name##_f) SIG;                                    \
  static RET NTAPI name##_bridge SIG;                                          \
  RET NTAPI name SIG;                                                          \
  conprx::PatchRequest *name##_patch() { return &patch_requests_[pr##Name]; }  \
  RET name##_imposter SIG { return (name##_patch()->get_imposter<name##_f>()) ARGS; }
  FOR_EACH_LPC_FUNCTION(__DECLARE_REPLACEMENT_FUNCTION__)
#undef __DECLARE_REPLACEMENT_FUNCTION__

  // A message number that, if seen by the message infrastructure, is used to
  // configure the infrastructure and not sent.
  static const ulong_t kGetConsoleCPApiNumber = 0x3C;

  handler_t handler_;
  bool enabled_;

  // Setting this to true enables the special message handling but only for
  // the next call -- the variable will be set back to false as soon as it has
  // been used.
  bool one_shot_special_handler_;

  // Data associated with locating ConsoleClientCallServer.
  bool is_locating_cccs_;
  fat_bool_t locate_cccs_result_;
  cccs_f cccs_;
  handle_t locate_cccs_port_handle_;

  // Data associated with the calibration message.
  static const ulong_t kCalibrationApiNumber = 0xDECADE;
  bool is_calibrating_;
  fat_bool_t calibrate_result_;
  capture_buffer_data_t *calibration_capbuf_;

  // The transform to apply when sending addresses through the console port.
  AddressXform port_xform_;
  AddressXform port_xform() { return port_xform_; }
  handle_t console_server_port_handle_;

  conprx::PatchSet *patches() { return &patches_; }
  conprx::PatchSet patches_;

  static Interceptor *current_;
  static Interceptor *current() { return current_; }
};

// The lpc message types should match internal types used by the LPC system.
// They've been pieced together from a bunch of different places, including
// ReactOS and random blog posts.

typedef uint8_t client_id_t[IF_32_BIT(8, 16)];

// The generic part of an LPC message.
struct port_message_data_t {
  union {
    struct {
      uint16_t data_length;
      uint16_t total_length;
    } s1;
    ulong_t length;
  } u1;
  union {
    struct {
      ushort_t type;
      ushort_t data_info_offset;
    } s2;
    ulong_t zero_init;
  };
  union {
    client_id_t client_id;
    uint64_t force_alignment;
  };
  ulong_t message_id;
  union {
    size_t client_view_size;
    ulong_t callback_id;
  };
};

// The header of a capture buffer.
struct capture_buffer_data_t {
  ulong_t length;
  capture_buffer_data_t *related_capture_buffer;
  ulong_t count_message_pointers;
  char *free_space;
  ulong_t *message_pointer_offsets[1];
};

// A platform integer.
typedef IF_32_BIT(int32_t, int64_t) intn_t;
typedef IF_32_BIT(uint32_t, uint64_t) uintn_t;

struct get_console_mode_m {
  void *handle;
  uint32_t mode;
};

typedef get_console_mode_m set_console_mode_m;

struct get_console_title_m {
  union {
    // It appears that this field is used for two different purposes: as a
    // request it indicates the size in bytes of the buffer to fill, as a
    // response it's the number of whole character written. Be careful not to
    // get the two uses confused.
    uint32_t size_in_bytes_in;
    uint32_t length_in_chars_out;
  };
  ONLY_64_BIT(uint32_t padding);
  void *title;
  bool is_unicode;
};

typedef get_console_title_m set_console_title_m;

struct get_console_cp_m {
  uint32_t code_page_id;
  bool_t is_output;
};

typedef get_console_cp_m set_console_cp_m;

// The max number of bytes we're willing to transport inline in write messages.
// This is the size that makes the struct's size match up with what we receive
// from the windows console api.
static const size_t kMaxInlineBytes = 80;

struct write_console_m {
  handle_t output;
  union {
    uint8_t inline_ansi_buffer[kMaxInlineBytes];
    uint16_t inline_wide_buffer[kMaxInlineBytes / sizeof(uint16_t)];
  };
  void *contents;
  uint32_t size_in_bytes;
  void *padding_1;
  bool is_inline;
  bool is_unicode;
  bool padding_2;
  int32_t unused_3;
};

struct read_console_m { };

struct message_data_header_t {
  capture_buffer_data_t *capture_buffer;
  uint32_t api_number;
  int32_t return_value;
  intn_t reserved;
};

struct get_console_screen_buffer_info_m {
  handle_t output;
  coord_t size;
  coord_t cursor_position;
  coord_t window_top_left;
  word_t  attributes;
  coord_t window_extent;
  coord_t maximum_window_size;
  word_t popup_attributes;
  bool_t fullscreen_supported;
  colorref_t color_table[16];
};

// How these are actually declared in the windows implementation I have no idea
// but using 4-packing seems to produce a struct packing that matches the data
// we get passed both on 32- and 64-bit.
#ifdef IS_MSVC
#  pragma pack(push, 4)
#endif

// A console api message, a superset of a port message.
struct message_data_t {
  port_message_data_t port_message;
  message_data_header_t header;
  union {
#define __EMIT_ENTRY__(Name, name, DLL, API) name##_m name;
  FOR_EACH_LPC_TO_INTERCEPT(__EMIT_ENTRY__)
#undef __EMIT_ENTRY__
  } payload;
};

#ifdef IS_MSVC
#  pragma pack(pop)
#endif

// Given the data size for a message returns the total length.
static size_t total_message_length_from_data_length(size_t data_length) {
  // I have no idea where that extra 4 bytes comes from on 32 bit. It suggests
  // that the port message portion is larger than what can be accounted for by
  // the struct but the header and payload are in the right place so there's
  // no room for it to be larger. Also it doesn't seem to be alignment because
  // all sizes are 4-aligned and those extra 4 bytes sometimes knocks it out
  // of 8-alignment. Go figure.
  return sizeof(lpc::port_message_data_t) + data_length + IF_32_BIT(4, 0);
}

// Computes the joint api number given a dll and an api index.
#define CALC_API_NUMBER(DLL, API) (((DLL) << 16) | (API))

// A wrapper around an LPC message that encapsulates access to the various
// fields of interest.
class Message {
public:
  Message(message_data_t *request, message_data_t *reply,
      Interceptor *interceptor, AddressXform xform);

  // The total size of the message data, including header and full payload.
  uint16_t total_length() { return data()->port_message.u1.s1.total_length; }

  // Just the size of the payload, excluding the header size.
  uint16_t data_length() { return data()->port_message.u1.s1.data_length; }

  // Misc accessors.
  uint32_t api_number() { return data()->header.api_number; }

  // The raw underlying data.
  message_data_t *data() { return request_; }

  // Sets the return status to the given value.
  void set_return_value(conprx::NtStatus status);

  // Dump this message textually to the given out stream.
  void dump(tclib::OutStream *out, tclib::Blob::DumpStyle style = tclib::Blob::DumpStyle());

  AddressXform xform() { return xform_; }

  Interceptor *interceptor() { return interceptor_; }

  conprx::NtStatus send_to_backend();

private:
  message_data_t *request_;
  message_data_t *reply_;
  Interceptor *interceptor_;
  AddressXform xform_;
};

} // namespace lpc

#endif // _AGENT_LPC_HH
