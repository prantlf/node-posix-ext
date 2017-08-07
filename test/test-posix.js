// tests the posix methods
'use strict';
var expect = require('chai').expect,
    posix = require('../lib/posix-ext');

describe('posix', function () {
  it('exports an object', function () {
    // the posix object is exposed and the getgrgid method, which is
    // originally available only on POSIX, is available on Windows too
    expect(posix).to.be.an('object');
  });

  it('exposes getgrgid', function () {
    expect(posix.getgrgid).to.be.a('function');
  });

  describe('getgrgid', function () {
    before(function () {
      if (process.platform.match(/^win/i)) {
        expect(posix.process).to.be.an('object');
      } else {
        expect(posix.process).to.be.equal(process);
      }
      expect(posix.process.getgid).to.be.a('function');
      this.group = posix.getgrgid(posix.process.getgid());
    });

    it('returns name', function () {
      expect(this.group).to.be.an('object');
      expect(this.group.name).to.be.a('string');
      expect(this.group.name.length).to.be.above(0);
    });

    it('returns gid', function () {
      // tests of this module should run first of all on Windows; they run on
      // POSIX too to check that the same functionality is available there too
      if (process.platform.match(/^win/i)) {
        // check for SIDs on Windows
        expect(this.group.gid).to.be.a('string');
        expect(this.group.gid).to.have.string('S-');
      } else {
        // check for integers on POSIX
        expect(this.group.gid).to.be.a('number');
        expect(this.group.gid).to.not.be.below(0);
      }
    });
  });

  it('exposes getgrnam', function () {
    expect(posix.getgrnam).to.be.a('function');
  });

  describe('getgrnam', function () {
    before(function () {
      // test with an always available administrative group
      var name = process.platform.match(/^win/i) ? "Administrators" : "root";
      this.group = posix.getgrnam(name);
    });

    it('returns name', function () {
      expect(this.group).to.be.an('object');
      expect(this.group.name).to.be.a('string');
      expect(this.group.name.length).to.be.above(0);
    });

    it('returns gid', function () {
      // tests of this module should run first of all on Windows; they run on
      // POSIX too to check that the same functionality is available there too
      if (process.platform.match(/^win/i)) {
        // check for SIDs on Windows
        expect(this.group.gid).to.be.a('string');
        expect(this.group.gid).to.have.string('S-');
      } else {
        // check for integers on POSIX
        expect(this.group.gid).to.be.a('number');
        expect(this.group.gid).to.not.be.below(0);
      }
    });
  });

  it('exposes getpwuid', function () {
    expect(posix.getpwuid).to.be.a('function');
  });

  describe('getpwuid', function () {
    before(function () {
      if (process.platform.match(/^win/i)) {
        expect(posix.process).to.be.an('object');
      } else {
        expect(posix.process).to.be.equal(process);
      }
      expect(posix.process.getuid).to.be.a('function');
      this.user = posix.getpwuid(posix.process.getuid());
    });

    it('returns name', function () {
      expect(this.user).to.be.an('object');
      expect(this.user.name).to.be.a('string');
      expect(this.user.name.length).to.be.above(0);
    });

    it('returns gid', function () {
      // tests of this module should run first of all on Windows; they run on
      // POSIX too to check that the same functionality is available there too
      if (process.platform.match(/^win/i)) {
        // check for SIDs on Windows
        expect(this.user.uid).to.be.a('string');
        expect(this.user.uid).to.have.string('S-');
        expect(this.user.gid).to.be.a('string');
        expect(this.user.gid).to.have.string('S-');
      } else {
        // check for integers on POSIX
        expect(this.user.uid).to.be.a('number');
        expect(this.user.uid).to.not.be.below(0);
        expect(this.user.gid).to.be.a('number');
        expect(this.user.gid).to.not.be.below(0);
      }
    });
  });

  it('exposes getpwnam', function () {
    expect(posix.getpwnam).to.be.a('function');
  });

  describe('getpwnam', function () {
    before(function () {
      // test with an always available administrative user
      var name = process.platform.match(/^win/i) ? "Administrator" : "root";
      this.user = posix.getpwnam(name);
    });

    it('returns name', function () {
      expect(this.user).to.be.an('object');
      expect(this.user.name).to.be.a('string');
      expect(this.user.name.length).to.be.above(0);
    });

    it('returns gid', function () {
      // tests of this module should run first of all on Windows; they run on
      // POSIX too to check that the same functionality is available there too
      if (process.platform.match(/^win/i)) {
        // check for SIDs on Windows
        expect(this.user.uid).to.be.a('string');
        expect(this.user.uid).to.have.string('S-');
        expect(this.user.gid).to.be.a('string');
        expect(this.user.gid).to.have.string('S-');
      } else {
        // check for integers on POSIX
        expect(this.user.uid).to.be.a('number');
        expect(this.user.uid).to.not.be.below(0);
        expect(this.user.gid).to.be.a('number');
        expect(this.user.gid).to.not.be.below(0);
      }
    });
  });
});
