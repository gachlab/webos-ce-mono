#include "qglcompat.h"

#include <QHash>
#include <QOpenGLFunctions>

namespace {

QHash<QOpenGLContext*, QGLContext*>& wrappers()
{
    static QHash<QOpenGLContext*, QGLContext*> map;
    return map;
}

} // namespace

// Reached through a static function only, so the private constructor stays
// private to everyone else.
const QGLContext* QGLContext::currentContext()
{
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context)
        return nullptr;

    QGLContext*& wrapper = wrappers()[context];
    if (!wrapper) {
        wrapper = new QGLContext(context);
        QObject::connect(context, &QOpenGLContext::aboutToBeDestroyed, context, [context]() {
            delete wrappers().take(context);
        });
    }
    return wrapper;
}

GLuint QGLContext::bindTexture(const QImage& image, GLenum target, GLint format, BindOptions options)
{
    if (!m_context || QOpenGLContext::currentContext() != m_context || image.isNull())
        return 0;

    // Qt 5 uploaded premultiplied data only when asked to. RGBA8888 is RGBA
    // byte order on every platform, which is what GL_RGBA/GL_UNSIGNED_BYTE reads.
    QImage upload = image.convertToFormat((options & PremultipliedAlphaBindOption)
                                          ? QImage::Format_RGBA8888_Premultiplied
                                          : QImage::Format_RGBA8888);
    if (options & InvertedYBindOption)
        upload = upload.flipped(Qt::Vertical);

    QOpenGLFunctions* gl = m_context->functions();
    GLuint id = 0;
    gl->glGenTextures(1, &id);
    gl->glBindTexture(target, id);

    const GLint filter = (options & LinearFilteringBindOption) ? GL_LINEAR : GL_NEAREST;
    gl->glTexParameteri(target, GL_TEXTURE_MIN_FILTER,
                        (options & MipmapBindOption) ? GL_LINEAR_MIPMAP_LINEAR : filter);
    gl->glTexParameteri(target, GL_TEXTURE_MAG_FILTER, filter);
    gl->glTexImage2D(target, 0, format, upload.width(), upload.height(), 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, upload.constBits());
    if (options & MipmapBindOption)
        gl->glGenerateMipmap(target);

    return id;
}

GLuint QGLContext::bindTexture(const QPixmap& pixmap, GLenum target, GLint format, BindOptions options)
{
    return bindTexture(pixmap.toImage(), target, format, options);
}

void QGLContext::deleteTexture(GLuint id)
{
    if (!m_context || QOpenGLContext::currentContext() != m_context || !id)
        return;
    m_context->functions()->glDeleteTextures(1, &id);
}
