# Core and RPC without exceptions

The core and scalar RPC backend support GCC/Clang `-fno-exceptions`. This does not
claim no-exception support for Python, Node-API, or every optional backend.

`PORTAL_NO_EXCEPTION_STRICT=1` requires noexcept declarations on converters
invoked through `to_cast` and `from_optional`. The default relaxed policy (`0`)
accepts unannotated converters. Both disable throwing when compiler exceptions
are off; relaxed mode does not catch exceptions or weaken input validation.

Use `rpc::detail::try_decode_request(bytes, request&)` for checked decoding.
`try_decode_response(bytes, value&, string&)` distinguishes `ok`, `malformed`,
and `remote_error`. Output values are published only after the complete frame
passes validation. The error string is updated on a valid remote-error response.
These helpers belong to the existing experimental RPC codec API, not a stable
production transport abstraction.

The RPC router uses optional argument dispatch, returning an encoded error for
bad arity/types and unknown methods. Ordinary conversion mismatch remains
separate from runtime/API failure. With exceptions enabled, runtime exceptions
continue to become remote error responses.

Legacy must-succeed operations, including throwing decode/from_cast wrappers,
keep throwing with exceptions enabled; without exceptions they assert and abort
on contract failure, including under NDEBUG. Standard allocator exhaustion is
process-fatal without exceptions. Checked decoding does not guarantee recoverable
OOM. Consistently configure this header-only library across translation units.
