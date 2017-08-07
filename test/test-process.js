// tests the posix.process methods
'use strict';
var expect = require('chai').expect,
    posix = require('../lib/posix-ext');

describe('process', function () {
  before(function () {
    expect(posix).to.be.an('object');
    this.process = posix.process;
  });

  it('is exposed as an object', function () {
    // the process object is exposed and the getgid method
    // is available on all platforms
    expect(this.process).to.be.equal(process);
  });

  it('exposes getgid', function () {
    expect(this.process.getgid).to.be.a('function');
  });

  describe('getgid', function () {
    before(function () {
      this.gid = this.process.getgid();
    });

    it('returns gid', function () {
      // tests of this module should run first of all on Windows; they run on
      // POSIX too to check that the same functionality is available there too
      if (process.platform.match(/^win/i)) {
        // check for SID on Windows
        expect(this.gid).to.be.a('string');
        expect(this.gid).to.have.string('S-');
      } else {
        // check for an integer on POSIX
        expect(this.gid).to.be.a('number');
        expect(this.gid).to.not.be.below(0);
      }
    });
  });

  it('exposes getgroups', function () {
    expect(this.process.getgroups).to.be.a('function');
  });

  describe('getgroups', function () {
    before(function () {
      this.groups = this.process.getgroups();
    });

    it('returns an array', function () {
      expect(this.groups).to.be.an('array');
    });

    it('returns at least one group', function () {
      // check that the list of groups of the calling process is not empty
      expect(this.groups.length).to.be.above(0);
    });

    it('returns the primary process group', function () {
      // check that the primary process group is included
      // in the supplementary group list
      expect(this.process.getgid).to.be.a('function');
      var gid = this.process.getgid();
      expect(this.groups).to.include(gid);
    });

    it('returns valid groups', function () {
      // check that a group entry is valid
      var gid = this.groups[0];
      // tests of this module should run first of all on Windows; they run on
      // POSIX too to check that the same functionality is available there too
      if (process.platform.match(/^win/i)) {
        // check for SID on Windows
        expect(gid).to.be.a('string');
        expect(gid).to.have.string('S-');
      } else {
        // check for an integer on POSIX
        expect(gid).to.be.a('number');
        expect(gid).to.not.be.below(0);
      }
    });
  });

  it('exposes getuid', function () {
    expect(this.process.getuid).to.be.a('function');
  });

  describe('getuid', function () {
    before(function () {
      this.uid = this.process.getuid();
    });

    it('returns uid', function () {
      // tests of this module should run first of all on Windows; they run on
      // POSIX too to check that the same functionality is available there too
      if (process.platform.match(/^win/i)) {
        // check for SID on Windows
        expect(this.uid).to.be.a('string');
        expect(this.uid).to.have.string('S-');
      } else {
        // check for an integer on POSIX
        expect(this.uid).to.be.a('number');
        expect(this.uid).to.not.be.below(0);
      }
    });
  });
});
