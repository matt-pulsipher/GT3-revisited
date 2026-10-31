#pragma once

// gt3r --help: the command line, the screens --screen opens (from the screen registry) and the
// testing variables (GT3R_*), printed before SDL starts. Keep it in step with App::init's
// argument handling and the variables' readers when either changes.

namespace gt3r {

// --help or -h among the arguments.
bool wants_launch_help(int argc, char** argv);

// The help text on stdout (Windows: the console gt3r was started from).
void print_launch_help();

} // namespace gt3r
