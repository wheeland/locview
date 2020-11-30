#include "treemapwidget.h"

#include <memory>
#include <algorithm>

#include "squarify.h"

#include <QtMath>
#include <QPaintEvent>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QDebug>
#include <QScopedArrayPointer>
#include <QFontMetrics>

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLExtraFunctions>

using SquarifyNode = Squarify::TreeMapNode;
using Squarify::Rect;

const char *s_vs = "\
    attribute vec2 pos; \
    attribute vec4 rect; \
    attribute vec4 bgColor; \
    attribute vec4 fadeColor; \
    uniform vec2 screenSize; \
    uniform vec2 offset; \
    uniform float scale; \
    varying vec4 v_bgColor; \
    varying vec4 v_fadeColor; \
    varying vec2 v_uv; \
    void main() { \
        v_bgColor = bgColor; \
        v_fadeColor = fadeColor; \
        v_uv = pos; \
        vec2 viewPos = (rect.xy + pos * rect.zw + offset) * scale; \
        gl_Position = vec4(vec2(-1.0, 1.0) + vec2(2.0, -2.0) * viewPos / screenSize, 0.0, 1.0); \
    }\
";

const char *s_fs = "\
    uniform float border; \
    varying vec4 v_bgColor; \
    varying vec4 v_fadeColor; \
    varying vec2 v_uv; \
    void main() { \
        vec2 d = abs(v_uv - vec2(0.5)); \
        vec2 f = pow(vec2(1.0) - 2.0 * d, vec2(border)); \
        vec2 v = smoothstep(vec2(-0.5), vec2(1.0), f); \
        gl_FragColor = vec4(mix(v_fadeColor, v_bgColor, v.x * v.y)); \
    }\
";

TreeMapWidget::TreeMapWidget(QWidget *parent)
    : QOpenGLWidget(parent)
    , TreeMapLayouter(width(), height())
    , m_oldSize(width(), height())
    , m_quadVertexBuffer(QOpenGLBuffer::VertexBuffer)
    , m_nodeInstanceBuffer(QOpenGLBuffer::VertexBuffer)
    , m_groupInstanceBuffer(QOpenGLBuffer::VertexBuffer)
{
    setMouseTracking(true);
    connect(&m_resizeTimer, &QTimer::timeout, this, &TreeMapWidget::onResize);
    m_resizeTimer.setSingleShot(true);
}

TreeMapWidget::~TreeMapWidget()
{
}

void TreeMapWidget::resizeEvent(QResizeEvent *event)
{
    QOpenGLWidget::resizeEvent(event);

    if (!m_resizeTimer.isActive())
        m_oldSize = event->oldSize();
    m_resizeTimer.start(100);
}

void TreeMapWidget::onResize()
{
    m_oldSize = QSize(width(), height());
    TreeMapLayouter::resize(width(), height());
}

void TreeMapWidget::initializeGL()
{
    if (!m_shader.addShaderFromSourceCode(QOpenGLShader::Vertex, s_vs)
            || !m_shader.addShaderFromSourceCode(QOpenGLShader::Fragment, s_fs)
            || !m_shader.link()) {
        qWarning() << m_shader.log();
    }
    m_shaderLocPos = m_shader.attributeLocation("pos");
    m_shaderLocRect = m_shader.attributeLocation("rect");
    m_shaderLocBgColor = m_shader.attributeLocation("bgColor");
    m_shaderLocFadeColor = m_shader.attributeLocation("fadeColor");

    float vertices[12] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0};
    m_quadVertexBuffer.create();
    m_quadVertexBuffer.bind();
    m_quadVertexBuffer.allocate(vertices, 12 * sizeof(float));

    m_nodeInstanceBuffer.create();
    m_groupInstanceBuffer.create();
}

class Buffer
{
public:
    Buffer(uint sz = 1 << 24) : m_reserved(sz), m_bytes(new quint8[sz]) {}
    void clear() { m_size = 0; }
    const void *data() const { return (const void*) m_bytes.get(); }
    int size() const { return m_size; }

    void reserve(uint sz) {
        resize(m_size + sz);
        m_available += sz;
    }

    Buffer &operator<<(quint8 v) { write(v); return *this; }
    Buffer &operator<<(float v) { write(v); return *this; }
    Buffer &operator<<(const QColor &color) {
        write<quint8>(color.red());
        write<quint8>(color.green());
        write<quint8>(color.blue());
        write<quint8>(color.alpha());
        return *this;
    }

private:
    void resize(uint n) {
        if (n < m_reserved)
            return;

        uint newSize = m_reserved;
        while (newSize <= n)
            newSize *= 2;

        quint8 *newData = new quint8[newSize];
        memcpy(newData, m_bytes.get(), m_size);
        m_bytes.reset(newData);
        m_reserved = newSize;
    }

    template <class T> void write(T v) {
        assert(m_size + sizeof(T) <= m_available);
        void *dst = (void*) &m_bytes[m_size];
        assert(((alignof (T) - 1) & (uintptr_t) dst) == 0);
        *((T*) dst) = v;
        m_size += sizeof(T);
    }

    uint m_reserved = 0;
    uint m_available = 0;
    uint m_size = 0;
    QScopedArrayPointer<quint8> m_bytes;
};

class VertexBuffer : public Buffer
{
public:
    VertexBuffer() : Buffer() {}
    void add(const QRectF &rect, const QColor &bg, const QColor &fade)
    {
        const float x1 = rect.left();
        const float y1 = rect.top();
        const float w = rect.width();
        const float h = rect.height();

        reserve(stride());
        *this << x1 << y1 << w << h << bg << fade;
    }

    void upload(QOpenGLBuffer &glBuffer)
    {
        glBuffer.bind();
        glBuffer.allocate(data(), size());
    }

    int vertices() const { return size() / stride(); }
    constexpr static int stride() { return 24; }
};

void TreeMapWidget::paintGL()
{
    QOpenGLContext *gl = QOpenGLContext::currentContext();

    // while resizing, we refrain from updating the layout, but just scale the content
    // for a short amount of time
    const float scaleX = (float) width() / m_oldSize.width();
    const float scaleY = (float) height() / m_oldSize.height();
    const auto scale = [&](const QRectF &r) {
        return QRectF(r.x() * scaleX, r.y() * scaleY, r.width() * scaleX, r.height() * scaleY);
    };

    // if the node layout has changed, we need to re-build the vertex buffer
    if (m_nodeInstanceBufferDirty) {
        VertexBuffer vertices;
        traverseRenderNodes(*m_renderedNode, [&](const Node &node) {
            if (node.renderState == Render)
                vertices.add(node.sceneRect, node.color, QColor(0, 0, 0));
            return (node.renderState == RenderChildren);
        });
        vertices.upload(m_nodeInstanceBuffer);
        m_nodeInstancesCount = vertices.vertices();
        m_nodeInstanceBufferDirty = false;
    }

    const auto render = [&](QOpenGLBuffer &instanceBuffer, int instances, QPointF ofs, float scale, float border) {
        m_shader.bind();
        m_shader.setUniformValue("screenSize", QVector2D(m_oldSize.width(), m_oldSize.height()));
        m_shader.setUniformValue("border", border);
        m_shader.setUniformValue("offset", ofs);
        m_shader.setUniformValue("scale", scale);

        m_quadVertexBuffer.bind();
        m_shader.enableAttributeArray(m_shaderLocPos);
        m_shader.setAttributeBuffer(m_shaderLocPos, GL_FLOAT, 0, 2, 8);

        instanceBuffer.bind();
        m_shader.enableAttributeArray(m_shaderLocRect);
        m_shader.enableAttributeArray(m_shaderLocBgColor);
        m_shader.enableAttributeArray(m_shaderLocFadeColor);
        m_shader.setAttributeBuffer(m_shaderLocRect, GL_FLOAT, 0, 4, VertexBuffer::stride());
        m_shader.setAttributeBuffer(m_shaderLocBgColor, GL_UNSIGNED_BYTE, 16, 4, VertexBuffer::stride());
        m_shader.setAttributeBuffer(m_shaderLocFadeColor, GL_UNSIGNED_BYTE, 20, 4, VertexBuffer::stride());
        gl->extraFunctions()->glVertexAttribDivisor(m_shaderLocRect, 1);
        gl->extraFunctions()->glVertexAttribDivisor(m_shaderLocBgColor, 1);
        gl->extraFunctions()->glVertexAttribDivisor(m_shaderLocFadeColor, 1);

        gl->extraFunctions()->glDisable(GL_CULL_FACE);
        gl->extraFunctions()->glEnable(GL_BLEND);
        gl->extraFunctions()->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gl->extraFunctions()->glBlendEquation(GL_FUNC_ADD);
        gl->extraFunctions()->glDrawArraysInstanced(GL_TRIANGLES, 0, 6, instances);

        m_shader.disableAttributeArray(m_shaderLocPos);
        m_shader.disableAttributeArray(m_shaderLocRect);
        m_shader.disableAttributeArray(m_shaderLocBgColor);
        m_shader.disableAttributeArray(m_shaderLocFadeColor);
        gl->extraFunctions()->glVertexAttribDivisor(m_shaderLocRect, 0);
        gl->extraFunctions()->glVertexAttribDivisor(m_shaderLocBgColor, 0);
        gl->extraFunctions()->glVertexAttribDivisor(m_shaderLocFadeColor, 0);

        instanceBuffer.release();
        m_shader.release();
    };

    // Render all nodes
    render(m_nodeInstanceBuffer, m_nodeInstancesCount, -m_viewport.topLeft(), m_oldSize.width() / m_viewport.width(), 0.3f);

    // Render selected/highlighted outlines and node labels
    QPainter painter(this);
    const QColor paintColor(0, 0, 0);
    painter.setBrush(Qt::NoBrush);
    traverseRenderNodes(*m_renderedNode, [&](const Node &node) {
        if (node.renderState == Render) {
            // draw rect
            if (m_selectedNode == &node) {
                painter.setPen(QPen(paintColor, 3.0f));
                painter.drawRect(scale(node.viewRect.adjusted(0, 0, -1.5f, -1.5f)));
            }
            else if (m_hoveredNode == &node) {
                painter.setPen(QPen(paintColor, 2.0f));
                painter.drawRect(scale(node.viewRect.adjusted(0, 0, 1.0f, 1.0f)));
            }

            // possibly draw label text, if there is enough space
            if (node.viewRect.width() > 10 && node.viewRect.height() > 5) {
                painter.setPen(paintColor);
                const QRectF bounds = painter.boundingRect(node.viewRect, Qt::AlignHCenter | Qt::AlignCenter, node.label);
                if (bounds.width() < node.viewRect.width() + 10 && bounds.height() < node.viewRect.height() + 5) {
                    painter.drawText(scale(node.viewRect), Qt::AlignHCenter | Qt::AlignCenter, node.label);
                }
            }
        }
        return (node.renderState == RenderChildren);
    });

    // render group node outlines (blended on top)
    painter.beginNativePainting();
    VertexBuffer groupVertices;
    traverseRenderNodes(*m_renderedNode, [&](const Node &node) {
        if (!node.groupViewRect.isNull()) {
            groupVertices.add(node.groupViewRect, QColor(0, 0, 0, 0), QColor(0, 0, 0));
        }
        return node.responsibleForGroup;
    });
    groupVertices.upload(m_groupInstanceBuffer);
    render(m_groupInstanceBuffer, groupVertices.vertices(), QPointF(0, 0), 1.0f, 0.6f);
    painter.endNativePainting();

    // render group node labels
    painter.setPen(QPen(QColor(255, 255, 255), 1.0f));
    traverseRenderNodes(*m_renderedNode, [&](const Node &node) {
        if (!node.groupLabelRect.isNull())
            painter.drawText(scale(node.groupLabelRect), Qt::AlignCenter | Qt::AlignVCenter, node.groupLabel);
        return node.responsibleForGroup;
    });
}

void TreeMapWidget::wheelEvent(QWheelEvent *event)
{
    const float relx = (float) event->x() / (float) width();
    const float rely = (float) event->y() / (float) height();

    const float cx = m_viewport.left() + m_viewport.width() * relx;
    const float cy = m_viewport.top() + m_viewport.height() * rely;

    const float delta = qPow(0.5f, event->delta() / 1000.f);
    const float nw = m_viewport.width() * delta;
    const float nh = m_viewport.height() * delta;

    setViewport(QRectF(cx - relx * nw, cy - rely * nh, nw, nh));
}

void TreeMapWidget::keyPressEvent(QKeyEvent *event)
{
    const bool alt = (event->modifiers() & Qt::AltModifier);
    const bool ctrl = (event->modifiers() & Qt::ControlModifier);
    if (event->key() == Qt::Key_Backspace || (event->key() == Qt::Key_Left && (alt || ctrl))) {
        zoomOut();
    }
}

void TreeMapWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::BackButton) {
        zoomOut();
        return;
    }

    if (event->buttons() == Qt::LeftButton) {
        m_mouseDown = true;
        m_isPanning = false;
        m_mouseDownViewport = m_viewport;
        m_mouseDownViewPos = event->pos();
        m_mouseDownModelPos = viewToScene(event->pos());
    }

    if (event->buttons() == Qt::RightButton) {
        setSelectedNode(getNodeAt(event->pos(), m_renderedNode), event->pos());

        if (m_selectedNode) {
            emit nodeRightClicked(m_selectedNode->userData, mapToGlobal(event->pos()));
        }
    }
}

void TreeMapWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (const Node *node = getNodeAt(event->pos(), m_renderedNode)) {
            zoomIn(node->userData);
        }
    }
}

void TreeMapWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_mouseDown) {
        // ignore panning for a little while to accomodate for mouse clicks
        if (!m_isPanning) {
            const int dx = event->pos().x() - m_mouseDownViewPos.x();
            const int dy = event->pos().y() - m_mouseDownViewPos.y();
            const int dist2 = dx*dx + dy*dy;
            if (dist2 < 50)
                return;
            m_isPanning = true;
        }

        QPointF mouseAt = viewToScene(event->pos());
        QPointF delta = m_mouseDownModelPos - mouseAt;
        setViewport(m_viewport.translated(delta));
    }
    else {
        setHoveredNode(getNodeAt(event->pos(), m_renderedNode), event->pos());
    }
}

void TreeMapWidget::mouseReleaseEvent(QMouseEvent *event)
{
    // if we pressed left btn, but only barely moved the mouse, we count this as a click
    if (m_mouseDown && !m_isPanning) {
        setSelectedNode(getNodeAt(event->pos(), m_renderedNode), event->pos());
    }
    m_mouseDown = false;
}

QRectF TreeMapWidget::getTextBounds(const QString &text) const
{
    return QFontMetrics(font()).boundingRect(text);
}

void TreeMapWidget::onNodeTreeChanged()
{
    setHoveredNode(nullptr, QPoint());
    setSelectedNode(nullptr, QPoint());
    update();
}

void TreeMapWidget::onLayoutChanged()
{
    m_nodeInstanceBufferDirty = true;
    update();
}

void TreeMapWidget::onViewportChanged()
{
    m_nodeInstanceBufferDirty = true;
    update();
}

void TreeMapWidget::setSelectedNode(const Node *node, QPoint mouse)
{
    if (m_selectedNode != node) {
        m_selectedNode = node;
        update();
    }
    emit nodeSelected(node ? node->userData : nullptr, mouse);
}

void TreeMapWidget::setHoveredNode(const Node *node, QPoint mouse)
{
    if (m_hoveredNode != node) {
        m_hoveredNode = node;
        update();
    }
    emit nodeHovered(node ? node->userData : nullptr, mouse);
}
