glib_min_ver = '>=2.37.3'

libdeps = []
libdeps += dependency('libxfce4panel-2.0', version: '>=4.12.2', required: true)
libdeps += dependency('glib-2.0', version: glib_min_ver, required: true)
libdeps += dependency('gtk+-3.0', version: '>=3.3.16', required: true)
libdeps += dependency('xapp', version: '>=1.8.7', required: true)
libdeps += dependency('json-glib-1.0', version: '>=1.4.2', required: true)

xfce_plugin_sources = [
    'xapp-status-plugin.c',
    'status-icon.c',
]

xapp_status_plugin = shared_module('xapp-status-plugin',
    sources  : xfce_plugin_sources,
    include_directories: [top_inc],
    dependencies: libdeps,
    c_args: [
        '-Wno-declaration-after-statement',
        '-DG_LOG_DOMAIN="XAppStatusPlugin"',
        '-DVERSION="@0@"'.format(meson.project_version())
    ],
    link_args: [ '-Wl,-Bsymbolic', '-Wl,-z,relro', '-Wl,-z,now', '-lm'
    ],
    install: true,
    install_dir: join_paths(get_option('libdir'), 'xfce4', 'panel', 'plugins')
)

## Desktop file

i18n.merge_file(
    input: 'xapp-status-plugin.desktop.in',
    output: 'xapp-status-plugin.desktop',
    type: 'desktop',
    po_dir: join_paths(meson.source_root(), 'po'),
    install: true,
    install_dir: join_paths(get_option('datadir'), 'xfce4', 'panel', 'plugins')
)

install_data('configure.glade',
    install_dir: join_paths(get_option('datadir'), 'xfce4-xapp-status-plugin')
)

install_data('org.x.apps.xfce4-status-plugin.gschema.xml',
    install_dir: join_paths(get_option('datadir'), 'glib-2.0', 'schemas')
)

gnome.post_install(
    glib_compile_schemas: true
)
