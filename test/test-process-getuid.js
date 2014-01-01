// tests the process.getuid method
"use strict";
var assert = require('assert'),
    posix = require("../lib/posix-ext"),
    process = posix.process,
    uid;

// the process object is exposed and the getuid method, which is
// originally available only on POSIX, is available on Windows too
assert.ok(process);
assert.ok(process.getuid);
// check that the user identifier is a valid SID
uid = process.getuid();
// tests of this module should run first of all on Windows; they run on
// POSIX too to check that the same functionality is available there too
if (process.platform.match(/^win/i)) {
  // check for SID on Windows
  assert.strictEqual(typeof uid, "string");
  assert.strictEqual(uid.indexOf("S-"), 0);
} else {
  // check for an integer on POSIX
  assert.strictEqual(typeof uid, "number");
  assert.ok(uid >= 0);
}
