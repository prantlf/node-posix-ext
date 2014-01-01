// tests the process.getgid method
"use strict";
var assert = require('assert'),
    posix = require("../lib/posix-ext"),
    process = posix.process,
    gid;

// the process object is exposed and the getgid method, which is
// originally available only on POSIX, is available on Windows too
assert.ok(process);
assert.ok(process.getgid);
// check that the group identifier is a valid SID
gid = process.getgid();
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
