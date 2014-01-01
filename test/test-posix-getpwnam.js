// tests the posix.getpwnam method
"use strict";
var assert = require('assert'),
    posix = require("../lib/posix-ext"),
    process = posix.process,
    name, user;

// test with an always available administrative user
name = process.platform.match(/^win/i) ? "Administrator" : "root";
// the posix object is exposed and the getpwnam method, which is
// originally available only on POSIX, is available on Windows too
assert.ok(posix);
assert.ok(posix.getpwnam);
// check the user information
user = posix.getpwnam(name);
assert.ok(user);
assert.strictEqual(typeof user.name, "string");
assert.ok(user.name.length > 0);
// tests of this module should run first of all on Windows; they run on
// POSIX too to check that the same functionality is available there too
if (process.platform.match(/^win/i)) {
  // check for SIDs on Windows
  assert.strictEqual(typeof user.uid, "string");
  assert.strictEqual(user.uid.indexOf("S-"), 0);
  assert.strictEqual(typeof user.gid, "string");
  assert.strictEqual(user.gid.indexOf("S-"), 0);
} else {
  // check for integers on POSIX
  assert.strictEqual(typeof user.uid, "number");
  assert.ok(user.uid >= 0);
  assert.strictEqual(typeof user.gid, "number");
  assert.ok(user.gid >= 0);
}
