// QtOpenGL's QGL* classes, removed in Qt 6, on top of QOpenGL*.
//
// Only what the shell calls is here: the desktop build creates a QGLFormat and
// a QGLWidget for the viewport (WindowServer.cpp) and uploads window textures
// through QGLContext (HostWindowDataOpenGL.cpp). The rest of the QGL* users
// sit behind TARGET_DEVICE and only need their #include to resolve.
//
// Known differences from Qt 5, none of which the desktop build relies on:
//
//  - QGLWidget created its GL context in its constructor; QOpenGLWidget creates
//    it the first time the widget is shown. makeCurrent() right after
//    construction does nothing until then.
//  - QGLFramebufferObject is not a QPaintDevice any more. The device-only code
//    that opens a QPainter on one would need QOpenGLPaintDevice.
//  - bindTexture() does not cache: every call uploads and returns a new name.
//    The shell deletes and re-binds on every call anyway.

#ifndef QGLCOMPAT_H
#define QGLCOMPAT_H

#include <QFlags>
#include <QImage>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLWidget>
#include <QPixmap>
#include <QSurfaceFormat>

class QGLFormat
{
public:
    QGLFormat() : m_format(QSurfaceFormat::defaultFormat()) {}

    void setSamples(int samples) { m_format.setSamples(samples); }

    // QGLFormat kept "sample buffers on" apart from the sample count, and took
    // 4 samples when only the first was set. QSurfaceFormat has only the count.
    void setSampleBuffers(bool enable)
    {
        if (!enable)
            m_format.setSamples(0);
        else if (m_format.samples() <= 0)
            m_format.setSamples(4);
    }

    void setRedBufferSize(int size) { m_format.setRedBufferSize(size); }
    void setGreenBufferSize(int size) { m_format.setGreenBufferSize(size); }
    void setBlueBufferSize(int size) { m_format.setBlueBufferSize(size); }
    void setAlphaBufferSize(int size) { m_format.setAlphaBufferSize(size); }

    // Direct rendering has no QSurfaceFormat counterpart: every context Qt 6
    // creates is direct where the platform allows it.
    void setDirectRendering(bool) {}
    void setDoubleBuffer(bool enable)
    {
        m_format.setSwapBehavior(enable ? QSurfaceFormat::DoubleBuffer : QSurfaceFormat::SingleBuffer);
    }

    // Same split as sample buffers: alpha on, with a size only if one is given.
    void setAlpha(bool enable)
    {
        if (!enable)
            m_format.setAlphaBufferSize(0);
        else if (m_format.alphaBufferSize() <= 0)
            m_format.setAlphaBufferSize(8);
    }

    static void setDefaultFormat(const QGLFormat& format) { QSurfaceFormat::setDefaultFormat(format.m_format); }

    QSurfaceFormat toSurfaceFormat() const { return m_format; }
    static QGLFormat fromSurfaceFormat(const QSurfaceFormat& format)
    {
        QGLFormat f;
        f.m_format = format;
        return f;
    }

private:
    QSurfaceFormat m_format;
};

class QGLContext
{
public:
    enum BindOption {
        NoBindOption                = 0x0000,
        InvertedYBindOption         = 0x0001,
        MipmapBindOption            = 0x0002,
        PremultipliedAlphaBindOption = 0x0004,
        LinearFilteringBindOption   = 0x0008,
        DefaultBindOption           = LinearFilteringBindOption | InvertedYBindOption | MipmapBindOption
    };
    Q_DECLARE_FLAGS(BindOptions, BindOption)

    // A context of its own, as QGLContext(format) + create() made one in Qt 5.
    explicit QGLContext(const QGLFormat& format);
    ~QGLContext();
    bool create(const QGLContext* shareContext = nullptr);
    // QGLWidget::context() is QOpenGLWidget's here, which hands out the
    // QOpenGLContext itself.
    bool create(QOpenGLContext* shareContext);
    QGLFormat format() const { return m_format; }

    // A QGLContext standing for whichever QOpenGLContext is current, or null
    // when none is. The same QOpenGLContext always yields the same object, and
    // the object goes away with the context.
    static const QGLContext* currentContext();

    QOpenGLContext* contextHandle() const { return m_context; }
    bool isValid() const { return m_context && m_context->isValid(); }

    // Uploads the image as a new texture in this context and returns its name,
    // or 0 if this context is not the current one. The data is handed to GL as
    // RGBA bytes; "format" is the internal format, as in Qt 5.
    GLuint bindTexture(const QImage& image, GLenum target = GL_TEXTURE_2D,
                       GLint format = GL_RGBA, BindOptions options = DefaultBindOption);
    GLuint bindTexture(const QPixmap& pixmap, GLenum target = GL_TEXTURE_2D,
                       GLint format = GL_RGBA, BindOptions options = DefaultBindOption);
    void deleteTexture(GLuint id);

private:
    explicit QGLContext(QOpenGLContext* context) : m_context(context), m_owned(false) {}

    QOpenGLContext* m_context;
    QGLFormat m_format;
    bool m_owned;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(QGLContext::BindOptions)

class QGLWidget : public QOpenGLWidget
{
public:
    explicit QGLWidget(QWidget* parent = nullptr) : QOpenGLWidget(parent) {}

    explicit QGLWidget(const QGLFormat& format, QWidget* parent = nullptr)
        : QOpenGLWidget(parent)
    {
        setFormat(format.toSurfaceFormat());
    }

    // QOpenGLWidget always creates its own context; the one handed in only
    // contributes its format, and sharing comes from
    // Qt::AA_ShareOpenGLContexts instead.
    explicit QGLWidget(QGLContext* context, QWidget* parent = nullptr)
        : QOpenGLWidget(parent)
    {
        if (context)
            setFormat(context->format().toSurfaceFormat());
    }

    QGLFormat format() const { return QGLFormat::fromSurfaceFormat(QOpenGLWidget::format()); }
};

class QGLFramebufferObject : public QOpenGLFramebufferObject
{
public:
    using QOpenGLFramebufferObject::QOpenGLFramebufferObject;
};

#endif // QGLCOMPAT_H
