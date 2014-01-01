#ifndef PROCESS_WIN_H
#define PROCESS_WIN_H

#include <node.h>
#include <v8.h>

namespace process_win {

using namespace node;
using namespace v8;

// to be called during the node add-on initialization
void init(Handle<Object> target);

} // namespace process_win

#endif // PROCESS_WIN_H
