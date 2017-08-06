"use strict";

// merges all members from the source object to the target object;
// it's like the underscore.extend
function merge(target, source) {
  var key;
  for (key in source) {
    if (source.hasOwnProperty(key)) {
      target[key] = source[key];
    }
  }
}

// prefer the fs-ext module if available, otherwise
// load the built-in fs module
var fs = (function () {
      var modules = [ "fs-ext", "fs" ],
          i;
      for (i = 0; i < modules.length; ++i) {
        try {
          return require(modules[i]);
        } catch (error) {
          if (error.code !== "MODULE_NOT_FOUND") {
            throw error;
          }
        }
      }
      throw "Cannot load the fs module";
    }());

// implement the POSIX metods with the help of the native add-on
// on Windows; the code for POSIX platforms is at the end of the file
if (process.platform.match(/^win/i)) {
  (function () {
    // the result of fs.readlink is the exact string used when the link
    // was created by `ln -s`, which can be a relative path; however,
    // the path was relative to the current directory when the command
    // was executed; not to the path of the link; if it wasn't so, this
    // method will resolve to an invalid path and that's wahy you should
    // always create links using the absolute target path
    function resolveLink(fpath, lpath) {
      // check if the path is absolute on both Windows and POSIX platforms
      if (/^([a-z]:)?[\/\\]/i.test(lpath)) {
        return lpath;
      }
      return path.join(path.dirname(fpath), lpath);
    }

    var path = require("path"),

        // load the native add-on; prefer the release version, but try
        // the debug to to make the development more convenient
        binding = require('bindings')('posix-ext'),

        // declare the extra methods for the built-in process object
        // which provide the POSIX functionality on Windows
        processExt = (function () {
          return {
            // process.getuid returning a SID
            getuid: function() {
              return binding.getuid();
            },

            // process.getgid returning a SID
            getgid:  function() {
              return binding.getgid();
            },

            // process.getgroups returning names of supplementary groups
            // for the current process on Windows, including the primary
            // group; the names are in the format "domain\account"
            getgroups: function() {
              return binding.getgroups();
            }
          };
        }()),

        // declare the extra methods for the built-in fs module
        // which provide the POSIX functionality on Windows
        fsExt = (function () {

          // merges the ownership to the stats
          function completeStats(stats, fd, callback) {
            // allow calling with both fd and path
            (typeof fd === "string" ? binding.getown :
              binding.fgetown)(fd, function(error, ownership) {
              if (error) {
                callback(error);
              } else {
                // replace the uid and gid members in the original stats
                // with the values containing SIDs
                merge(stats, ownership);
                callback(undefined, stats);
              }
            });
          }

          // merges the ownership to the stats
          function completeStatsSync(stats, fd) {
            // allow calling with both fd and path
            var ownership = (typeof fd === "string" ?
              binding.getown : binding.fgetown)(fd);
            // replace the uid and gid members in the original stats
            // with the values containing SIDs
            merge(stats, ownership);
            return stats;
          }

          return {
            // fs.fstat returning uid and gid as SIDs
            fstat: function(fd, callback) {
              // get the built-in stats which work on Windows too
              fs.fstat(fd, function(error, stats) {
                if (error) {
                  callback(error);
                } else {
                  // replace the ownership information (uid and gid)
                  // with the data useful on Windows - principal SIDs
                  completeStats(stats, fd, callback);
                }
              });
            },

            // fs.fstatSync returning uid and gid as SIDs
            fstatSync: function(fd) {
              // get the built-in stats which work on Windows too
              var stats = fs.fstatSync(fd);
              // replace the ownership information (uid and gid)
              // with the data useful on Windows - principal SIDs
              return completeStatsSync(stats, fd);
            },

            // fs.stat returning uid and gid as SIDs
            stat: function(fpath, callback) {
              // get the built-in stats which work on Windows too
              fs.lstat(fpath, function(error, stats) {
                if (error) {
                  callback(error);
                } else {
                  // GetNamedSecurityInfo, which is used by binding.getown,
                  // doesn't resolve sybolic links automatically; do the
                  // resolution here and call the lstat implementation
                  if (stats.isSymbolicLink()) {
                    fs.readlink(fpath, function(error, lpath) {
                      if (error) {
                        callback(error);
                      } else {
                        fpath = resolveLink(fpath, lpath);
                        fsExt.lstat(fpath, callback);
                      }
                    });
                  } else {
                    // replace the ownership information (uid and gid)
                    // with the data useful on Windows - principal SIDs
                    completeStats(stats, fpath, callback);
                  }
                }
              });
            },

            // fs.statSync returning uid and gid as SIDs
            statSync: function(fpath) {
              // get the built-in stats which work on Windows too
              // GetNamedSecurityInfo, which is used by binding.getown,
              // doesn't resolve sybolic links automatically; do the
              // resolution here and call the lstat implementation
              var stats = fs.lstatSync(fpath);
              if (stats.isSymbolicLink()) {
                var lpath = fs.readlinkSync(fpath);
                fpath = resolveLink(fpath, lpath);
                return fsExt.lstatSync(fpath);
              }
              // replace the ownership information (uid and gid)
              // with the data useful on Windows - principal SIDs
              return completeStatsSync(stats, fpath);
            },

            // fs.lstat returning uid and gid as SIDs
            lstat: function(fpath, callback) {
              // get the built-in stats which work on Windows too
              fs.lstat(fpath, function(error, stats) {
                if (error) {
                  callback(error);
                } else {
                  // replace the ownership information (uid and gid)
                  // with the data useful on Windows - principal SIDs
                  completeStats(stats, fpath, callback);
                }
              });
            },

            // fs.lstatSync returning uid and gid as SIDs
            lstatSync: function(fpath) {
              // get the built-in stats which work on Windows too
              // GetNamedSecurityInfo, which is used by binding.getown,
              // doesn't resolve sybolic links automatically; it's
              // suitable for the lstat implementation as-is
              var stats = fs.lstatSync(fpath);
              // replace the ownership information (uid and gid)
              // with the data useful on Windows - principal SIDs
              return completeStatsSync(stats, fpath);
            },

            // fs.fchown accepting uid and gid as SIDs
            fchown: function(fd, uid, gid, callback) {
              binding.fchown(fd, uid, gid, function(error) {
                callback(error);
              });
            },

            // fs.fchownSync accepting uid and gid as SIDs
            fchownSync: function(fd, uid, gid) {
              binding.fchown(fd, uid, gid);
            },

            // fs.chown accepting uid and gid as SIDs
            chown: function(fpath, uid, gid, callback) {
              fs.lstat(fpath, function(error, stats) {
                if (error) {
                  callback(error);
                } else {
                  if (stats.isSymbolicLink()) {
                    fs.readlink(fpath, function(error, lpath) {
                      if (error) {
                        callback(error);
                      } else {
                        fpath = resolveLink(fpath, lpath);
                        fsExt.lchown(fpath, uid, gid, callback);
                      }
                    });
                  } else {
                    fsExt.lchown(fpath, uid, gid, callback);
                  }
                }
              });
            },

            // fs.chownSync accepting uid and gid as SIDs
            chownSync: function(fpath, uid, gid) {
              // SetNamedSecurityInfo, which is used by binding.chown,
              // doesn't resolve sybolic links automatically; do the
              // resolution here and call the lchown implementation
              var stats = fs.lstatSync(fpath);
              if (stats.isSymbolicLink()) {
                var lpath = fs.readlinkSync(fpath);
                fpath = resolveLink(fpath, lpath);
              }
              fsExt.lchownSync(fpath, uid, gid);
            },

            // fs.lchown accepting uid and gid as SIDs
            lchown: function(fpath, uid, gid, callback) {
              binding.chown(fpath, uid, gid, function(error) {
                callback(error);
              });
            },

            // fs.lchownSync accepting uid and gid as SIDs
            lchownSync: function(fpath, uid, gid) {
              // SetNamedSecurityInfo, which is used by binding.chown,
              // doesn't resolve sybolic links automatically; it's
              // suitable for the lchown implementation as-is
              binding.chown(fpath, uid, gid);
            }
          };
        }()),

        // declare the extra methods for the original posix module
        // which provide the POSIX functionality on Windows
        posixExt = (function () {
          return {
            // posix.getgrgid returning the gid as SID and the list of
            // the group members on Windows , the names are in the format
            // "domain\account"
            getgrgid: function(gid) {
              return binding.getgrgid(gid);
            },

            // posix.getgrnam returning the gid as SID and the list of
            // the group members on Windows , the name is in the format
            // "domain\account"
            getgrnam: function(name) {
              return binding.getgrnam(name);
            },

            // posix.getpwnam returning the uid and gid as SIDs and the gecos
            // and dir members filled accordingly on Windows; the name is in
            // the format "domain\account"
            getpwnam: function(name) {
              return binding.getpwnam(name);
            },

            // posix.getpwuid returning the uid and gid as SIDs and the gecos
            // and dir members filled accordingly on Windows; the name is in
            // the format "domain\account"
            getpwuid: function(uid) {
              return binding.getpwuid(uid);
            }
          };
        }());

    // fill the exports of this module with the methods of the original
    // posix module which have their compatible counterparts implemented
    // in this module
    merge(exports, posixExt);

    // add the process member providing a drop-in replacement for the
    // built-in process object and patch some of its methods with their
    // cross-platform versions from this module
    exports.process = {};
    merge(exports.process, process);
    merge(exports.process, processExt);

    // add the fs member providing a drop-in replacement for the
    // built-in fs module and patch some of its methods with their
    // cross-platform versions from this module
    exports.fs = {};
    merge(exports.fs, fs);
    merge(exports.fs, fsExt);

  }());
} else {
  // provide the compatible interface on POSIX platforms; the
  // implementation doesn't need the native add-on on POSIX
  (function () {
    var posix = require("posix"),
    // offer methods accepting uid and gid under their original
    // POSIX names for completeness of the interface
        posixExt = {
          getgrgid: posix.getgrnam,
          getpwuid: posix.getpwnam
        };

    // fill the exports of this module with the methods of the
    // original posix module and the extras from this module
    merge(exports, posix);
    merge(exports, posixExt);

    // add the process member providing a drop-in replacement for the
    // built-in process object; no changes, just offering the same
    // module interface as on Windows
    exports.process = process;

    // add the fs member providing a drop-in replacement for the
    // built-in fs module; no changes, just offering the same
    // module interface as on Windows
    exports.fs = fs;
  }());
}
