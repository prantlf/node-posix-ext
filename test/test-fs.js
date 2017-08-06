// tests the posix.fs methods stat and chown
'use strict';
var expect = require('chai').expect,
    path = require('path'),
    posix = require('../lib/posix-ext'),
    process = posix.process,
    fs = posix.fs,
    space = 'tmp-test-fs',
    uname, gname, uid, gid, wname, wid, permitted;

// test with an always available administrative user and group
uname = process.platform.match(/^win/i) ? 'Guest' : 'nobody';
uid = posix.getpwnam(uname).uid;
gname = process.platform.match(/^win/i) ? 'Guests' : 'nogroup';
gid = posix.getgrnam(gname).gid;
wname = process.platform.match(/^win/i) ? 'BUILTIN\\Administrators' : 'root';
wid = posix.getgrnam(wname).gid;

function setUp() {
  fs.mkdirSync(space);
  var fd = fs.openSync(space + '/file', 'w');
  fs.closeSync(fd);
  var fpath = fs.realpathSync(space + '/file');
  fs.symlinkSync(fpath, space + '/file_link');
  fs.chownSync(space + '/file', uid, gid);
  fs.mkdirSync(space + '/directory');
  fpath = fs.realpathSync(space + '/directory');
  fs.symlinkSync(fpath, space + '/directory_link');
  fs.chownSync(space + '/directory', uid, gid);
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
  permitted = describe;
} catch (error) {
  if (error.code != 'EPERM') {
    throw error;
  }
  permitted = describe.skip;
}

describe('fs', function () {
  before(function () {
    expect(posix).to.be.an('object');
    this.fs = posix.fs;
  });

  it('is exposed as an object', function () {
    // the fs object is exposed and the stat method method
    // is available on all platforms
    expect(this.fs).to.be.an('object');
  });

  it('exposes statSync', function () {
    expect(this.fs.statSync).to.be.a('function');
  });

  permitted('statSync', function () {
    it('checks a file with link resolution', function () {
      var stats = this.fs.statSync(space + '/file');
      expect(stats.isFile()).to.equal(true);
      expect(stats.uid).to.equal(uid);
      expect(stats.gid).to.equal(gid);
    });

    it('checks a directory with link resolution', function () {
      var stats = this.fs.statSync(space + '/directory');
      expect(stats.isDirectory()).to.equal(true);
      expect(stats.uid).to.equal(uid);
      expect(stats.gid).to.equal(gid);
    });

    it('check that a file link resolves automatically', function () {
      var stats = this.fs.statSync(space + '/file_link');
      expect(stats.isFile()).to.equal(true);
      expect(stats.uid).to.equal(uid);
      expect(stats.gid).to.equal(gid);
    });

    it('check that a directory link resolves automatically', function () {
      var stats = this.fs.statSync(space + '/directory_link');
      expect(stats.isDirectory()).to.equal(true);
      expect(stats.uid).to.equal(uid);
      expect(stats.gid).to.equal(gid);
    });
  });

  it('exposes stat', function () {
    expect(this.fs.stat).to.be.a('function');
  });

  permitted('stat', function () {
    it('checks a file with link resolution', function (done) {
      this.fs.stat(space + '/file', function (error, stats) {
        expect(stats.isFile()).to.equal(true);
        expect(stats.uid).to.equal(uid);
        expect(stats.gid).to.equal(gid);
        done();
      });
    });

    it('checks a directory with link resolution', function (done) {
      this.fs.stat(space + '/directory', function (error, stats) {
        expect(stats.isDirectory()).to.equal(true);
        expect(stats.uid).to.equal(uid);
        expect(stats.gid).to.equal(gid);
        done();
      });
    });

    it('check that a file link resolves automatically', function (done) {
      this.fs.stat(space + '/file_link', function (error, stats) {
        expect(stats.isFile()).to.equal(true);
        expect(stats.uid).to.equal(uid);
        expect(stats.gid).to.equal(gid);
        done();
      });
    });

    it('check that a directory link resolves automatically', function (done) {
      this.fs.stat(space + '/directory_link', function (error, stats) {
        expect(stats.isDirectory()).to.equal(true);
        expect(stats.uid).to.equal(uid);
        expect(stats.gid).to.equal(gid);
        done();
      });
    });
  });

  it('exposes lstatSync', function () {
    expect(this.fs.lstatSync).to.be.a('function');
  });

  permitted('lstatSync', function () {
    it('checks a file without link resolution', function () {
      var stats = this.fs.lstatSync(space + '/file');
      expect(stats.isFile()).to.equal(true);
      expect(stats.uid).to.equal(uid);
      expect(stats.gid).to.equal(gid);
    });

    it('checks a directory without link resolution', function () {
      var stats = this.fs.lstatSync(space + '/directory');
      expect(stats.isDirectory()).to.equal(true);
      expect(stats.uid).to.equal(uid);
      expect(stats.gid).to.equal(gid);
    });

    it('check that a file link does not resolve automatically', function () {
      var stats = this.fs.lstatSync(space + '/file_link');
      expect(stats.isSymbolicLink()).to.equal(true);
      //expect(stats.uid).to.equal(uid);
      //expect(stats.gid).to.equal(gid);
    });

    it('check that a directory link does not resolve automatically', function () {
      var stats = this.fs.lstatSync(space + '/directory_link');
      expect(stats.isSymbolicLink()).to.equal(true);
      //expect(stats.uid).to.equal(uid);
      //expect(stats.gid).to.equal(gid);
    });
  });

  it('exposes lstat', function () {
    expect(this.fs.lstat).to.be.a('function');
  });

  permitted('lstat', function () {
    it('checks a file without link resolution', function (done) {
      this.fs.lstat(space + '/file', function (error, stats) {
        expect(stats.isFile()).to.equal(true);
        expect(stats.uid).to.equal(uid);
        expect(stats.gid).to.equal(gid);
        done();
      });
    });

    it('checks a directory without link resolution', function (done) {
      this.fs.lstat(space + '/directory', function (error, stats) {
        expect(stats.isDirectory()).to.equal(true);
        expect(stats.uid).to.equal(uid);
        expect(stats.gid).to.equal(gid);
        done();
      });
    });

    it('check that a file link does not resolve automatically', function (done) {
      this.fs.lstat(space + '/file_link', function (error, stats) {
        expect(stats.isSymbolicLink()).to.equal(true);
        //expect(stats.uid).to.equal(uid);
        //expect(stats.gid).to.equal(gid);
        done();
      });
    });

    it('check that a directory link does not resolve automatically', function (done) {
      this.fs.lstat(space + '/directory_link', function (error, stats) {
        expect(stats.isSymbolicLink()).to.equal(true);
        //expect(stats.uid).to.equal(uid);
        //expect(stats.gid).to.equal(gid);
        done();
      });
    });
  });
});
