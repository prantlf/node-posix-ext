"use strict";
// the posix module is not available on Windows, where the native
// add-on from this posix-ext module will step in
if (!process.platform.match(/^win/i)) {
  try {
    // if the module has been already installed, just leave
    require("posix");
  } catch (error) {
    // if the module loading failed with an unexpected error, bail out
    if (error.code !== "MODULE_NOT_FOUND") {
      throw error;
    }
    (function () {
      // install the module locally; if you widh it globally, install
      // the module before installing this posix-ext module
      var exec = require("child_process").exec,
          child = exec("npm install posix");
      // propagate output of the module installer on the current console
      child.stdout.pipe(process.stdout);
      child.stderr.pipe(process.stderr);
      child.on("exit", function (code, signal) {
        var error;
        if (code !== 0) {
          error = new Error(code, "Installing the posix module failed");
          error.code = code;
          error.signal = signal;
          throw error;
        }
      });
    }());
  }
}
