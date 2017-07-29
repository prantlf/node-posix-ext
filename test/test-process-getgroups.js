// tests the process.getgroups method
"use strict";
var assert = require('assert'),
    posix = require("../lib/posix-ext"),
    process = posix.process,
    groups, gid;

// the process object is exposed and the getgroups method, which is
// originally available only on POSIX, is available on Windows too
assert.ok(process);
if (typeof process.getgroups === "undefined") {
  console.warn("  process.getgroups not available; ignoring the tests");
  console.warn("  (running node.js < 0.10 on a POSIX platform?)");
  return;
}
 assert.ok(process.getgroups);
// check that the list of groups of the calling process is not empty
groups = process.getgroups();
assert.ok(Array.isArray(groups));
assert.ok(groups.length > 0);
// check that a group entry is valid
gid = groups[0];
// tests of this module should run first of all on Windows; they run on
// POSIX too to check that the same functionality is available there too
if (process.platform.match(/^win/i)) {
  // check for SID on Windows
  assert.strictEqual(typeof gid, "string");
  assert.strictEqual(gid.indexOf("S-"), 0);
} else {
  // check for an integer on POSIX
  assert.strictEqual(typeof gid, "number");
  assert.ok(gid >= 0);
}
// check that the primary process group is included
// in the supplementary group list
gid = process.getgid();
assert.ok(groups.indexOf(gid) >= 0);
