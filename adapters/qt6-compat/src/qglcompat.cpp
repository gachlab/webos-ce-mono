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

QGLContext::QGLContext(const QGLFormat& format)
    : m_context(new QOpenGLContext), m_format(format), m_owned(true)
{
    m_context->setFormat(format.toSurfaceFormat());
}

QGLContext::~QGLContext()
{
    if (m_owned)
        delete m_context;
}

bool QGLContext::create(const QGLContext* shareContext)
{
    if (shareContext && shareContext->contextHandle())
        m_context->setShareContext(shareContext->contextHandle());
    return m_context->create();
}

bool QGLContext::create(QOpenGLContext* shareContext)
{
    if (shareContext)
        m_context->setShareContext(shareContext);
    return m_context->create();
}

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
        // mirrored(), not flipped(Qt::Vertical): flipped() arrived after Qt 6.8,
        // which is what Debian stable ships, and CI on trixie caught it here.
        // They are the same call underneath -- flipped(Qt::Vertical) is
        // mirrored_helper(false, true), which is what mirrored(false, true)
        // reaches too -- so this compiles on 6.8 and 6.10 alike with no version
        // guard. Qt marks mirrored() deprecated from 6.13; when that lands, this
        // is where the #if goes.
        upload = upload.mirrored(false, true);

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
