#!/usr/bin/env python3
"""Explicitly isolated codec/router harness; NOT a generated-binder integration build.

This is used when only the fetched, hash-verified production headers are available.
Normal tests include export_callable.h and rpc.h directly instead. No timed codec
or router body is replaced. Only unrelated backend/template declarations are
excluded; the fixtures use the existing legacy Binder interface.
"""
from pathlib import Path
import argparse
p = argparse.ArgumentParser()
p.add_argument('root', type=Path)
p.add_argument('output', type=Path)
a = p.parse_args()
s = (a.root/'portal/backends/rpc.h').read_text()
prelude = '''#include "portal/optional.h"
namespace portal {
template<class T> struct type_identity { using type = T; };
constexpr std::size_t dynamic_arity = static_cast<std::size_t>(-1);
template<class> struct is_transactional_export_binder : std::false_type {};
template<class> struct is_export_overload_binder : std::false_type {};
}
'''
s = s.replace('#include "../error_policy.h"', '#include "portal/error_policy.h"')
s = s.replace('#include "../backend_base.h"', prelude)
marker = '    struct rpc_backend : backend_base<rpc_backend> {'
assert s.count(marker) == 1
s = s.split(marker)[0] + '} // namespace portal\n#endif\n'
a.output.parent.mkdir(parents=True, exist_ok=True)
a.output.write_text(s)
