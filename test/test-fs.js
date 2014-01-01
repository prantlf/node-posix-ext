// tests the fs methods stat and chown
"use strict";
var assert = require('assert'),
    path = require("path"),
    posix = require("../lib/posix-ext"),
    process = posix.process,
    fs = posix.fs,
    space = "tmp-test-fs",
    uname, gname, uid, gid, stats;

// test with an always available administrative user and group
uname = process.platform.match(/^win/i) ? "Guest" : "nobody";
uid = posix.getpwnam(uname).uid;
gname = process.platform.match(/^win/i) ? "Guests" : "nogroup";
gid = posix.getgrnam(gname).gid;

function setUp() {
  fs.mkdirSync(space);
  var fd = fs.openSync(space + "/file", "w", "0666");
  fs.closeSync(fd);
  var fpath = fs.realpathSync(space + "/file");
  fs.symlinkSync(fpath, space + "/file_link");
  fs.chownSync(space + "/file", uid, gid);
  fs.mkdirSync(space + "/directory");
  fpath = fs.realpathSync(space + "/directory", "0777");
  fs.symlinkSync(fpath, space + "/directory_link");
  fs.chownSync(space + "/directory", uid, gid);
}

function tearDown() {
  if (fs.existsSync(space)) {
    var children = fs.readdirSync(space);
    for (var i = 0; i < children.length; ++i) {
      var fpath = path.join(space, children[i]),
          stats = fs.lstatSync(fpath);
      if (stats.isDirectory()) {
        fs.rmdirSync(fpath);
      } else {
        fs.unlinkSync(fpath);
      }
    }
    fs.rmdirSync(space);
  }
}

try {
  tearDown();
  setUp();
} catch (error) {
  if (error.code == "EPERM") {
    console.warn("  no permission to perform chown; ignoring the tests");
    return;
  } else {
    throw error;
  }
}

// check the testing file
stats = fs.statSync(space + "/file");
assert.ok(stats.isFile());
assert.strictEqual(stats.uid, uid);
assert.strictEqual(stats.gid, gid);
fs.stat(space + "/file", function (error, stats) {
  assert.ok(stats.isFile());
  assert.strictEqual(stats.uid, uid);
  assert.strictEqual(stats.gid, gid);
});
stats = fs.lstatSync(space + "/file");
assert.ok(stats.isFile());
assert.strictEqual(stats.uid, uid);
assert.strictEqual(stats.gid, gid);
fs.lstat(space + "/file", function (error, stats) {
  assert.ok(stats.isFile());
  assert.strictEqual(stats.uid, uid);
  assert.strictEqual(stats.gid, gid);
});

// check that the testing file link resolves automatically
stats = fs.statSync(space + "/file_link");
assert.ok(stats.isFile());
assert.strictEqual(stats.uid, uid);
assert.strictEqual(stats.gid, gid);
fs.stat(space + "/file_link", function (error, stats) {
  assert.ok(stats.isFile());
  assert.strictEqual(stats.uid, uid);
  assert.strictEqual(stats.gid, gid);
});
// check the testing file link without the automatic resolution
stats = fs.lstatSync(space + "/file_link");
assert.notEqual(stats.uid, uid);
assert.notEqual(stats.gid, gid);
fs.lstat(space + "/file_link", function (error, stats) {
  assert.ok(stats.isSymbolicLink());
  assert.notEqual(stats.uid, uid);
  assert.notEqual(stats.gid, gid);
});

// check the testing directory
stats = fs.statSync(space + "/directory");
assert.ok(stats.isDirectory());
assert.strictEqual(stats.uid, uid);
assert.strictEqual(stats.gid, gid);
fs.stat(space + "/directory", function (error, stats) {
  assert.ok(stats.isDirectory());
  assert.strictEqual(stats.uid, uid);
  assert.strictEqual(stats.gid, gid);
});
stats = fs.lstatSync(space + "/directory");
assert.ok(stats.isDirectory());
assert.strictEqual(stats.uid, uid);
assert.strictEqual(stats.gid, gid);
fs.lstat(space + "/directory", function (error, stats) {
  assert.ok(stats.isDirectory());
  assert.strictEqual(stats.uid, uid);
  assert.strictEqual(stats.gid, gid);
});

// check that the testing directory link resolves automatically
stats = fs.statSync(space + "/directory_link");
assert.ok(stats.isDirectory());
assert.strictEqual(stats.uid, uid);
assert.strictEqual(stats.gid, gid);
fs.stat(space + "/directory_link", function (error, stats) {
  assert.ok(stats.isDirectory());
  assert.strictEqual(stats.uid, uid);
  assert.strictEqual(stats.gid, gid);
});
// check the testing directory link without the automatic resolution
stats = fs.lstatSync(space + "/directory_link");
assert.notEqual(stats.uid, uid);
assert.notEqual(stats.gid, gid);
fs.lstat(space + "/directory_link", function (error, stats) {
  assert.ok(stats.isSymbolicLink());
  assert.notEqual(stats.uid, uid);
  assert.notEqual(stats.gid, gid);
});
