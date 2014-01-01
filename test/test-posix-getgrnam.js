// tests the posix.getgrnam method
"use strict";
var assert = require('assert'),
    posix = require("../lib/posix-ext"),
    process = posix.process,
    name, group;

// test with an always available administrative group
name = process.platform.match(/^win/i) ? "Administrators" : "root";
// the posix object is exposed and the getpwuid method, which is
// originally available only on POSIX, is available on Windows too
assert.ok(posix);
assert.ok(posix.getgrnam);
// check the group information
group = posix.getgrnam(name);
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
