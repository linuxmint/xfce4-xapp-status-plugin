project('xfce4-xapp-status-plugin',
    'c',
    version : '1.0'
)

gettext_package = meson.project_name()

gnome = import('gnome')
pkg = import('pkgconfig')
i18n = import('i18n')

add_global_arguments([
    '-Wunused',
    '-Wimplicit-function-declaration'
  ],
  language: 'c'
)

if not get_option('deprecated_warnings')
  add_global_arguments([
      '-Wno-deprecated-declarations',
      '-Wno-deprecated',
      '-Wno-declaration-after-statement',
    ],
    language: 'c',
  )
endif

add_project_arguments('-DGETTEXT_PACKAGE="@0@"'.format(gettext_package), language:'c')

top_inc = include_directories('.')

subdir('plugin')
subdir('po')
