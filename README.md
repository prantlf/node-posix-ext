# node-posix-ext

A drop-in replacement for the Node.js modules process, fs and posix
providing their POSIX functionality on both POSIX and Windows platforms.

[![NPM version](https://badge.fury.io/js/node-posix-ext.png)](http://badge.fury.io/js/node-posix-ext) [![Build Status](https://api.travis-ci.org/prantlf/node-posix-ext.png)](http://travis-ci.org/prantlf/node-posix-ext) [![Dependency Status](https://david-dm.org/prantlf/node-posix-ext.svg)](https://david-dm.org/prantlf/node-posix-ext) [![devDependency Status](https://david-dm.org/prantlf/node-posix-ext/dev-status.svg)](https://david-dm.org/prantlf/node-posix-ext#info=devDependencies) [![Greenkeeper badge](https://badges.greenkeeper.io/prantlf/node-posix-ext.svg)](https://greenkeeper.io/)

[![NPM Downloads](https://nodei.co/npm/posix-ext.png?downloads=true&downloadRank=true&stars=true)](https://www.npmjs.com/package/posix-ext)[![NPM](https://nodei.co/npm-dl/posix-ext.png?months=6&height=3)](https://nodei.co/npm/posix-ext/)

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

## Installation

The installation of this module can be performed either from the GitHub
sources or simply from the NPM repository:

    npm install posix-ext

**For Windows users**: you will need a C++ compiler capable of compiling
native Node.js modules and Python 2.x, *before you install this module*.
If you do not have those, you can install them easily by installing the
NPM module [windows-build-tools](https://github.com/felixrieseberg/windows-build-tools)
globaly. Execute the following command in the PowerShell Console or Command
Prompt with elevated permissions ("Run as Administrator"`):

    npm install --global --production windows-build-tools

## Usage

This module provides patched objects for the `process`, `fs` and `posix`
modules, adding functionality on Windows and reusing the built-in one
on POSIX:

    var posix = require('../lib/posix-ext'),
        process = posix.process,
        fs = posix.fs;

## Process Calls on Windows

### process.getgid()

Returns the current process's group SID as string.

    // Prints "GID: S-1-5-32-513"
    console.log('GID:', process.getgid());

### process.getuid()

Returns the current process's user SID as string.

    // Prints "UID: S-1-5-21-3974217899-2981595321-1938156221-1011"
    console.log('UID:', process.getuid());

### process.getgroups()

Returns supplementary groups of the current process as an array of strings
with the group SIDs.

    console.log('Groups:', process.getgroups());

## POSIX Calls on Windows

### posix.getgrnam(group)

Gets the group information structured as a POSIX group database entry for
the given group. `group` can be specified as a string with a group name.

    var util = require('util');
    util.inspect(posix.getgrnam('NOTO\\Users'));

A sample output of the code above:

    { name: 'NOTO\\Users', passwd: 'x',
      gid: 'S-1-5-21-3974217899-2981595321-1938156221-513',
      members:
       [ 'NT AUTHORITY\\INTERACTIVE',
         'NT AUTHORITY\\Authenticated Users',
         'NOTO\\prantlf' ] }

### posix.getgruid(user)

Gets the group information structured as a POSIX group database entry for
the given group. `group` can be specified as a string with a SID.

    var group = posix.getgrnam('S-1-5-21-3974217899-2981595321-1938156221-513');

See `getgrnam` for more information.

### posix.getpwnam(user)

Gets the user information structured as a POSIX user database entry for
the given user. `user` can be specified as a string with a user name.

    var util = require('util');
    util.inspect(posix.getpwnam('NOTO\\prantlf'));

A sample output of the code above:

    { name: 'NOTO\\prantlf', passwd: 'x',
      uid: 'S-1-5-21-3974217899-2981595321-1938156221-1011',
      gid: 'S-1-5-32-513',
      gecos: 'Ferdinand Prantl',
      shell: '', dir: '' }

### posix.getpwuid(user)

Gets the user information structured as a POSIX user database entry for
the given user. `user` can be specified as a string with a SID.

    var user = posix.getpwnam('S-1-5-21-3974217899-2981595321-1938156221-1011');

See `getpwnam` for more information.

### posix.options: object

Exposes flags to control behavior of the methods above.

#### populateGroupMembers: boolean

Disables enumerating of group members to populate the `members` property
of the group information. If yoiu do not need it and work on a slow domain
network with a groups with many members, you may turn it off. The value
it `true` (populating members is enabled) by default.

    posix.options.populateGroupMembers = false;

## FileSystem Calls on Windows

Every method as also a synchronous alternative. Their names end with the
"Sync" suffix and they lack the last callback argument. If they succeed,
they return their result, instead of passing it to the second callback
argument. If they fail, they throw the error, instead of passing it
to the first callback argument.

### fs.stat(path, callback)

Refers to users and groups in the output `uid` and `gid` properties by their
SIDs (strings). See the original implementation for more infoemation.

### fs.lstat(path, callback)

Refers to users and groups in the output `uid` and `gid` properties by their
SIDs (strings). See the original implementation for more infoemation.

### fs.fstat(fd, callback)

Refers to users and groups in the output `uid` and `gid` properties by their
SIDs (strings). See the original implementation for more infoemation.

### fs.chown(path, uid, gid, callback)

Refers to users and groups in the input `uid` and `gid` arguments by their
SIDs (strings). See the original implementation for more infoemation.

### fs.lchown(path, uig, gid, callback)

Refers to users and groups in the input `uid` and `gid` arguments by their
SIDs (strings). See the original implementation for more infoemation.

### fs.fchown(fd, uid, gid, callback)

Refers to users and groups in the input `uid` and `gid` arguments by their
SIDs (strings). See the original implementation for more infoemation.

## Script Example

Output of the `example/example-whoami.js` run on Linux:

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

Output of the `example/example-whoami.js` run on Windows:

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

## Build

Make sure that you have [NodeJS] >= 4 and the global NPM module `node-gyp`
installed. Clone the Github repository to a local directory and enter it.
Install the package dependencies, build and test.

```shell
git clone https://github.com/prantlf/node-posix-ext.git
cd node-posix-ext
npm install
node-gyp configure
node-gyp build
npm test
```

## Contributing

In lieu of a formal styleguide, take care to maintain the existing coding
style. Add unit tests for any new or changed functionality.

First fork this repository and clone your fork locally instead of cloning
the original. See the "Build" chapter above for more details about how to
clone it and install the build dependencies.

Before you commit, check if tests succeed:

```shell
node-gyp configure
node-gyp build
npm test
```

Commit your changes to a separtate branch, so that you can create a pull
request for it:

```shell
git checkout -b <branch name>
git commit -a
git push origin <branch name>
```

## Release History

 * 2018-04-27   v0.4.5   Upgrade NPM module dependencies
 * 2017-08-07   v0.4.0   Allow disable populating group members
 * 2017-08-07   v0.3.0   Fix handling of files and users with non-ASCII characters
 * 2017-08-06   v0.2.0   Fix building for recent Node.js versions on Windows
 * 2017-07-29   v0.1.2   Fix building for Node.js versions >= 0.12 and <= 8
 * 2014-01-20   v0.1.1   Fix building with MSVC 2008
 * 2014-01-02   v0.1.0   Initial release

## License

Copyright (c) 2013-2018 Ferdinand Prantl

Licensed under the MIT license.
