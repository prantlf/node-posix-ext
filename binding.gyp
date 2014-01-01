{
  "targets": [
    {
      "target_name": "posix-ext",
      "sources": [
        "src/posix-ext.cc",
        "src/process-win.cc",
        "src/fs-win.cc",
        "src/posix-win.cc",
        "src/autores.cc",
        "src/winwrap.cc"
      ],
      "conditions" : [
        [
          "OS == 'win'", {
            "libraries" : [
              "netapi32.lib"
            ]
          }
        ]
      ]
    }
  ]
}
