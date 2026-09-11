# qt6-compat

What Qt 6 removed and webOS's code still uses, rebuilt on top of what Qt 6 has.
Same idea as `components/node-v8-shim`: the old code keeps calling the interface
it was written against, and the adapter maps it onto the modern one.

Only used when a component builds against Qt 6 (`WEBOS_QT_MAJOR=6`, which is what
`WEBOS_QT=6 tools/build-cmake.sh` sets). The Qt 5 build does not see it.

## What is here

- `include/qt6compat.h` — forced into every C++ file (`-include`). Brings back
  `qrand`/`qsrand`, `qSort`, `qFind`, `qVariantFromValue` and
  `qRegisterMetaTypeStreamOperators`, and includes `<QObject>`, which Qt 5's
  headers pulled in and several of HP's headers relied on.
- `include/QGLContext`, `QGLFormat`, `QGLWidget`, `QGLFramebufferObject` — the
  QtOpenGL classes, over `QOpenGLContext`, `QSurfaceFormat`, `QOpenGLWidget` and
  `QOpenGLFramebufferObject`. Only what the shell calls; `qglcompat.h` lists the
  behaviour that differs from Qt 5.

## What cannot be adapted from outside

Members removed from Qt's own classes cannot be put back without editing Qt:
`QFontMetrics::width`, `QImage::byteCount`, `QMap::insertMulti`,
`QList::toSet`, `QPainter::drawRoundRect`, `QChar` built from an enum, and so on.
Those call sites were changed instead, each to a form that compiles on Qt 5.15
and Qt 6 alike, so there is still one source for both builds.
