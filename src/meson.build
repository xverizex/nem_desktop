nem_desktop_sources = [
  'main.c',
  'main-window.c',
    'message-item.c',
    'register-window.c',
    'user-item.c',
    'cert.c'
  ]

nem_desktop_deps = [
    dependency('gio-2.0'),
  dependency('gtk4'),
  dependency('openssl'),
  dependency('json-glib-1.0'),
  dependency('gstreamer-1.0')
]

gnome = import('gnome')

nem_desktop_sources += gnome.compile_resources('nem_desktop-resources',
  'nem_desktop.gresource.xml',
  c_name: 'nem_desktop'
)

executable('nem_desktop', nem_desktop_sources,
  dependencies: nem_desktop_deps,
  install: true,
)
