"use strict";

// require the module with cross-platform POSIX functionality
// it returns an object with the rest of POSIX functions
var posix = require("posix-ext"),
    // patch the built-in process object to get the POSIX functions
    process = posix.process,
    // get identifiers (IDs on POSIX, SIDs on Windows) of the user
    // of this process and its primary group
    uid = process.getuid(),
    user = posix.getpwuid(uid),
    group = posix.getgrgid(user.gid);

// print the limited information about the user of this process
// and its primary group
console.log("user:  ", user);
console.log("group: ", group);
