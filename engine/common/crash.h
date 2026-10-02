// On Windows, a crash prints the exception and a stack trace with function
// names and source lines (from the build's .pdb) before the process ends.
#pragma once

namespace crash {

void install();

}  // namespace crash
