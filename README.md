# node-posix-ext

A drop-in replacement for the Node.js modules process, fs and posix
providing their POSIX functionality on both POSIX and Windows platforms.

[![Build Status](https://secure.travis-ci.org/prantlf/node-posixßext.png)](http://travis-ci.org/prantlf/node-posix-ext)

## Motivation

[Node.js](http://nodejs.org) exposes POSIX APIs, among the others.
Some of them are implemented on Windows, some are not.  When writing
a cross-platform code, the latter must be wrapped by additional code,
because the Node.js built-ins do not offer a cross-platform feature.

Some POSIX APIs can be implemented on Windows too and can provide
the desired cross-platform functionality.  For example, the built-in
`process` object and the core `fs` module do.  The optional modules
[posix](https://github.com/melor/node-posix) and
[fs-ext](https://github.com/baudehlo/node-fs-ext) provide additional
POSIX APIs to complete the Node.js offer.  However, not all functions
are callable on Windows.

For example, [user and group identifiers in file
stats](http://nodejs.org/api/fs.html#fs_class_fs_stats) are (integer)
numbers on POSIX platforms, while Windows represents them with
[SIDs](http://msdn.microsoft.com/en-us/library/windows/desktop/aa379594.aspx).
The type is different, but otherwise the primer is the same.  Why not
using the same methods on both platforms?  User and group identifiers
are "black boxes" for the caller anyway; meaning their actual data type...

## Features

The following methods expect and/or return user and group identifiers
(uid, gid) and this module implements them to handle and/or produce
SIDs on windows, while defaulting to the original functionality in POSIX:

    process: getuid, getgid, getgroups
    fs:      stat, lstat, fstat, chown, lchown, fchown
    posix:   getpwnam, getpwuid, getgrnam, getgrgid

## Usage

The installation can be performed either from the GitHub sources
or from the node module repository:

    npm install posix-ext

This module provides patched objects with the modified functionality
on Windows and the built-in on POSIX:

    var posix = require("../lib/posix-ext"),
        process = posix.process,
        fs = posix.fs;

## Example

Ouptut of the `example/example-whoami.js` run on Linux:

    $ node node_modules/posix-ext/example/example-whoami.js
    user:   { name: 'prantlf', passwd: 'x',
      uid: 1000, gid: 1000,
      gecos: 'Ferdinand Prantl,,,prantlf@gmail.com',
      shell: '/bin/bash', dir: '/home/prantlf' }
    group:  { name: 'prantlf', passwd: 'x', gid: 1000, members: [] }
    groups: [ 'adm (4)',
      'cdrom (24)',
      'sudo (27)',
      'dip (30)',
      'plugdev (46)',
      'lpadmin (109)',
      'sambashare (124)',
      'prantlf (1000)' ]

Ouptut of the `example/example-whoami.js` run on Windows:

    > node node_modules\posix-ext\example\example-whoami.js
    user:   { name: 'NOTO\\prantlf', passwd: 'x',
      uid: 'S-1-5-21-3974217899-2981595321-1938156221-1011',
      gid: 'S-1-5-32-513',
      gecos: 'Ferdinand Prantl',
      shell: '', dir: '' }
    group:  { name: 'NOTO\\Users', passwd: 'x',
      gid: 'S-1-5-21-3974217899-2981595321-1938156221-513',
      members:
       [ 'NT AUTHORITY\\INTERACTIVE',
         'NT AUTHORITY\\Authenticated Users',
         'NOTO\\prantlf' ] }
    groups: [ 'NOTO\\Users (S-1-5-21-3974217899-2981595321-1938156221-513)',
      'Everyone (S-1-1-0)',
      'BUILTIN\\Users (S-1-5-32-545)',
      'BUILTIN\\Remote Desktop Users (S-1-5-32-555)',
      'NT AUTHORITY\\INTERACTIVE (S-1-5-4)',
      'NT AUTHORITY\\Authenticated Users (S-1-5-11)',
      'LOCAL (S-1-2-0)',
      'NT AUTHORITY\\NTLM Authentication (S-1-5-64-10)' ]
