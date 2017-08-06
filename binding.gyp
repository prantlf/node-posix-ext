{
  "targets": [
    {
      "target_name": "posix-ext",
      "include_dirs" : [
        "<!(node -e \"require('nan')\")"
      ],
      "sources": [
        "src/posix-ext.cc",
        "src/autores.cc"
      ],
      "conditions" : [
        [
          "OS == 'win'", {
            "msvs_settings": {
              "VCCLCompilerTool": {
                "AdditionalOptions": [ "/EHsc" ]
              }
            },
            "sources": [
              "src/process-win.cc",
              "src/posix-win.cc",
              "src/fs-win.cc",
              "src/winwrap.cc"
            ],
            "libraries" : [
              "netapi32.lib"
            ]
          }
        ]
      ]
    }
  ]
}
