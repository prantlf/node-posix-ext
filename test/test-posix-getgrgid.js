// tests the posix.getgrgid method
"use strict";
var assert = require('assert'),
    posix = require("../lib/posix-ext"),
    process = posix.process,
    gid = process.getgid(),
    group;

// the posix object is exposed and the getpwuid method, which is
// originally available only on POSIX, is available on Windows too
assert.ok(posix);
assert.ok(posix.getgrgid);
// check the group information
group = posix.getgrgid(gid);
assert.ok(group);
assert.strictEqual(typeof group.name, "string");
assert.ok(group.name.length > 0);
// tests of this module should run first of all on Windows; they run on
// POSIX too to check that the same functionality is available there too
if (process.platform.match(/^win/i)) {
  // check for SIDs on Windows
  assert.strictEqual(typeof group.gid, "string");
  assert.strictEqual(group.gid.indexOf("S-"), 0);
} else {
  // check for integers on POSIX
  assert.strictEqual(typeof group.gid, "number");
  assert.ok(group.gid >= 0);
}
