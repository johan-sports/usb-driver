{
  'target_defaults': {

    'conditions': [
      ['OS=="win"', {
        'msvs_disabled_warnings': [
          4530,  # C++ exception handler used, but unwind semantics are not enabled
          4506,  # no definition for inline function
        ],
      }],
    ],
  },
  'targets': [
    {
      'target_name': 'usb_driver',
      'sources': [
        'src/usb_common.cc',
        'src/bindings.cc',
        'src/utils/logger.cc'
      ],
      'conditions': [
        ['OS=="mac"', {
          'sources': [
            'src/mac/usb_driver.cc',
            'src/mac/interop.cc'
          ],
          'cflags!': [ '-fno-exceptions' ],
          'cflags_cc!': [ '-fno-exceptions' ],
          'xcode_settings': {
            'MACOSX_DEPLOYMENT_TARGET': '10.9',
            'GCC_ENABLE_CPP_EXCEPTIONS': 'YES',        # -fno-exceptions
            'OTHER_LDFLAGS': [
              '-framework Foundation',
              '-framework IOKit',
              '-framework DiskArbitration'
            ],
          },
        }],
        ['OS=="win"', {
          'sources': [
            'src/win/usb_driver.cc',
          ],
          'link_settings': {
             'libraries': [
               'setupapi.lib'
             ]
          }
        }]
      ],
    }
  ]
}
