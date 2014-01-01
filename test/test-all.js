// runs all tests
"use strict";

var fs = require("fs");

// find all siblings of this file - the actual tests
fs.readdir(__dirname, function (error, tests) {
  var i, test;
  // if the directory listing failed, make all tests fail
  if (error) {
    throw error;
  } else {
    // go over all children (test files) found
    for (i = 0; i < tests.length; ++i) {
      test = tests[i];
      // skip this file, which is being executed now
      if (test !== "test-all.js") {
        console.log(test);
        // load and execute the test; if the test fails,
        // the error will end this script too
        require("./" + test);
      }
    }
  }
});
