project('xfce4-xapp-status-plugin', 'c', version : '0.4.4')

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

plugin_datadir = join_paths(get_option('prefix'), get_option('datadir'), meson.project_name())

gettext_package = meson.project_name()
locale_dir = join_paths(get_option('prefix'), get_option('localedir'))

add_project_arguments('-DGETTEXT_PACKAGE="@0@"'.format(gettext_package), language:'c')
add_project_arguments('-DLOCALEDIR="@0@"'.format(locale_dir), language:'c')
add_project_arguments('-DAPP_DATADIR="@0@"'.format(plugin_datadir), language:'c')

top_inc = include_directories('.')

subdir('plugin')
subdir('po')
