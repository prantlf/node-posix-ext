"use strict";

// require the module with cross-platform POSIX functionality;
// it returns an object with the rest of POSIX functions
var posix = require("posix-ext"),
    // patch the built-in process object to get the POSIX functions
    process = posix.process,
    // get identifiers (IDs on POSIX, SIDs on Windows) of the user
    // and primary and supplementary groups of this process
    uid = process.getuid(),
    gid = process.getgid(),
    gids = process.getgroups(),
    // get information about the user and the primary group
    user = posix.getpwuid(uid),
    group = posix.getgrgid(gid),
    // resolve identifiers of the supplementary groups to an array
    // of strings with the format: "<group name> (<group identifier>)"
    groups = gids.map(function (gid) {
        var group = posix.getgrgid(gid);
        return group && (group.name + " (" + group.gid + ")")
          || "Unknown (" + gid + ")";
      }, {});

// print the limited information about the user and primary
// and supplementary groups of this process
console.log("user:  ", user);
console.log("group: ", group);
console.log("groups:", groups);
