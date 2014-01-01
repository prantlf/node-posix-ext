#ifndef POSIX_WIN_H
#define POSIX_WIN_H

#include <node.h>
#include <v8.h>

namespace posix_win {

using namespace node;
using namespace v8;

// to be called during the node add-on initialization
void init(Handle<Object> target);

} // namespace posix_win

#endif // POSIX_WIN_H
