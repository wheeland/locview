#include "treemapwidget.h"

#include <memory>
#include <algorithm>

#include "hsluv-c/src/hsluv.h"
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

static constexpr float GROUP_LABEL_OFFSET = 0.5f;

using SquarifyNode = Squarify::TreeMapNode;
using Squarify::Rect;

const char *s_vs = "\
    attribute vec2 pos; \
    attribute vec2 uv; \
    attribute vec4 bgColor; \
    attribute vec4 fadeColor; \
    uniform vec2 screenSize; \
    varying vec4 v_bgColor; \
    varying vec4 v_fadeColor; \
    varying vec2 v_uv; \
    void main() { \
        v_bgColor = bgColor; \
        v_fadeColor = fadeColor; \
        v_uv = uv; \
        gl_Position = vec4(vec2(-1.0, 1.0) + vec2(2.0, -2.0) * pos / screenSize, 0.0, 1.0); \
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

static QRectF scaled(const QRectF &rect, float scale)
{
    return QRectF(rect.left() * scale, rect.top() * scale, rect.width() * scale, rect.height() * scale);
}

static QColor hv2qcolor(float hue, float value)
{
    double r, g, b;
    hsluv2rgb(hue, 100.0, value, &r, &g, &b);
    return QColor(255 * r, 255 * g, 255 * b);
}

static QRectF RectToQRect(const Rect &r) { return QRectF(r.x, r.y, r.width, r.height); }
static Rect QRectToRect(const QRectF &r) { return Rect(r.left(), r.top(), r.width(), r.height()); }

TreeMapWidget::TreeMapWidget(QWidget *parent)
    : QOpenGLWidget(parent)
    , m_vertexBuffer(QOpenGLBuffer::VertexBuffer)
{
    setMouseTracking(true);
}

TreeMapWidget::~TreeMapWidget()
{
}

void TreeMapWidget::setRootNode(const TreeMapNode &root)
{
    m_root.children.clear();
    m_zoomStack.clear();
    m_viewport = QRectF(0, 0, width(), height());
    rebuildNodeTree(m_root, root, 0);
    m_renderedNode = &m_root;
    relayoutTreeMapping(m_treeRoot, *m_renderedNode, m_viewport);
    updateCulling(m_treeRoot);
    updateGroupRendering(&m_treeRoot);
    update();
}

void TreeMapWidget::rebuildNodeTree(Node &dstNode, const TreeMapNode &srcNode, int depth)
{
    dstNode.label = srcNode.label;
    dstNode.groupLabel = srcNode.groupLabel;
    dstNode.groupLabelBounds = fontMetrics().boundingRect(dstNode.groupLabel);
    dstNode.groupLabelBounds.translate(-dstNode.groupLabelBounds.topLeft());
    dstNode.color = hv2qcolor(srcNode.hue, srcNode.value);
    dstNode.size = srcNode.size;
    dstNode.userData = srcNode.userData;

    dstNode.depth = depth;
    dstNode.children.resize(srcNode.children.size());
    for (int i  = 0; i < dstNode.children.size(); ++i) {
        rebuildNodeTree(dstNode.children[i], srcNode.children[i], depth + 1);
    }
}

void TreeMapWidget::relayoutTreeMapping(TreeNode &treeNode, Node &node, const QRectF &rect)
{
    treeNode.node = &node;
    treeNode.subdivisions.clear();
    node.sceneRect = rect;

    std::vector<float> inSizes;
    QVector<Node*> childNodes;
    for (Node &child : node.children) {
        if (child.size > 0.0f) {
            inSizes.push_back(child.size);
            childNodes.push_back(&child);
        }
    }

    if (childNodes.isEmpty())
        return;

    const float childTotal = std::accumulate(inSizes.begin(), inSizes.end(), 0);
    const bool hasUnchildishOverflow = (childTotal < node.size);
    if (hasUnchildishOverflow)
        inSizes.push_back(node.size - childTotal);

    const Rect inRect = QRectToRect(rect);
    const SquarifyNode tree = Squarify::Squarify(inSizes, inRect).computeWithHierarchy();

    int treeDepth = node.treeDepth;
    for (const SquarifyNode *treeIt = &tree; treeIt != nullptr; treeIt = treeIt->next.get()) {
        TreeNode::Subdivision subdivision;
        ++treeDepth;

        Q_ASSERT(!treeIt->elements.empty());
        for (const SquarifyNode::Element &element : treeIt->elements) {
            Node *childNode = childNodes[element.index];

            TreeNode subNode;
            childNode->treeDepth = treeDepth;
            relayoutTreeMapping(subNode, *childNode, RectToQRect(element.rect));
            subdivision.subnodes << subNode;
        }

        subdivision.remainingSceneRect = RectToQRect(treeIt->bounds);
        treeNode.subdivisions << subdivision;
    }

    if (hasUnchildishOverflow) {
        treeNode.subdivisions.back().subnodes.pop_back();
        if (treeNode.subdivisions.back().subnodes.empty())
            treeNode.subdivisions.pop_back();
    }
}

void TreeMapWidget::updateCulling(TreeNode &treeNode, bool fullyVisible)
{
    if (treeNode.node == nullptr)
        return;

    // cull against max. node depth
    const int relativeDepth = treeNode.node->depth - m_renderedNode->depth;
    if (m_maxDepth > 0 && relativeDepth > m_maxDepth) {
        treeNode.node->renderState = CulledDepth;
        return;
    }

    // cull against viewport, if necessary
    if (!fullyVisible) {
        if (!m_viewport.intersects(treeNode.node->sceneRect)) {
            treeNode.node->renderState = CulledViewport;
            return;
        }
        if (m_viewport.contains(treeNode.node->sceneRect))
            fullyVisible = true;
    }

    // project rect into view space
    const QRectF viewRect = sceneToView(treeNode.node->sceneRect);
    treeNode.node->viewRect = viewRect;

    // check whether we want to render the node directly, and
    // ignore the children it might have
    const bool tooSmall = viewRect.width() < m_maxSize || viewRect.height() < m_maxSize;
    const bool tooDeep = (m_maxDepth > 0 && relativeDepth >= m_maxDepth);
    const bool tooUnparenty = treeNode.node->children.isEmpty();
    if (tooSmall || tooDeep || tooUnparenty) {
        treeNode.node->renderState = Render;
        return;
    }

    treeNode.node->renderState = RenderChildren;
    for (TreeNode::Subdivision &sub : treeNode.subdivisions) {
        for (TreeNode &node : sub.subnodes) {
            updateCulling(node, fullyVisible);
        }
    }
}

void TreeMapWidget::updateGroupRendering(TreeNode *treeNode)
{
    if (treeNode->node == nullptr)
        return;

    treeNode->node->groupViewRect = QRectF();

    // if this node isn't rendering its children, exit early so we don't have to traverse the whole tree
    if (treeNode != &m_treeRoot && treeNode->node->renderState != RenderChildren) {
        treeNode->node->responsibleForGroup = false;
        return;
    }

    // we decided to render the node, now the question is whether it will be
    // rendered as a group node or not
    const float ratio = m_viewport.width() / width();
    const float minSceneSize = m_minGroupSize * ratio;
    const auto isPotentialGroup = [minSceneSize, ratio](Node &node, const QRectF &sceneRect) {
        return sceneRect.width() > (node.groupLabelBounds.width() + 2.f * GROUP_LABEL_OFFSET) * ratio
                && sceneRect.height() > (node.groupLabelBounds.height() + 2.f * GROUP_LABEL_OFFSET) * ratio
                && sceneRect.width() > minSceneSize
                && sceneRect.height() > minSceneSize;
    };

    bool canRenderSubdivsAsGroup = treeNode->node->responsibleForGroup;
    QVector<TreeNode::Subdivision> &subdivs = treeNode->subdivisions;
    for (int i = 0 ; i < subdivs.size(); ++i) {
        TreeNode::Subdivision &subdiv = subdivs[i];
        subdiv.remainingViewRect = sceneToView(subdiv.remainingSceneRect);

        // check whether all sub-nodes on this level could be rendered as groups
        for (TreeNode &node : subdiv.subnodes) {
            canRenderSubdivsAsGroup &= isPotentialGroup(*node.node, node.node->sceneRect);
        }

        // check if the other subdivisions would still have enough space for
        // rendering their unified group label, if this subdiv would be
        // rendered as independent groups
        if (i < subdivs.size() - 1 && !isPotentialGroup(*treeNode->node, subdivs[i + 1].remainingSceneRect)) {
            canRenderSubdivsAsGroup = false;
        }

        // if, and only if so, we shall permit it, for all of them
        for (TreeNode &node : subdiv.subnodes) {
            node.node->responsibleForGroup = canRenderSubdivsAsGroup;
        }

        // if this node is responsible for being rendered as a group,
        // but at least one sub-node within this subdivision is not eligible,
        // this means that we will have to draw the group on top of this node
        // (the group may possible only cover parts of it)
        if (treeNode->node->groupViewRect.isNull()
                && treeNode->node->responsibleForGroup
                && !canRenderSubdivsAsGroup) {
            treeNode->node->groupViewRect = subdiv.remainingViewRect;
        }
    }

    // update children
    for (TreeNode::Subdivision &subdiv : treeNode->subdivisions) {
        for (TreeNode &node : subdiv.subnodes) {
            updateGroupRendering(&node);
        }
    }
}

void TreeMapWidget::setMaxDepth(int maxDepth)
{
    if (m_maxDepth != maxDepth) {
        m_maxDepth = maxDepth;
        updateCulling(m_treeRoot);
        updateGroupRendering(&m_treeRoot);
        update();
    }
}

void TreeMapWidget::setMaxSize(int maxSize)
{
    maxSize = qMax(maxSize, 1);
    maxSize = qMin(maxSize, m_minGroupSize);
    if (m_maxSize != maxSize) {
        m_maxSize = maxSize;
        updateCulling(m_treeRoot);
        updateGroupRendering(&m_treeRoot);
        update();
    }
}

void TreeMapWidget::setMinGroupSize(int minGroupSize)
{
    minGroupSize = qMax(minGroupSize, 50);
    if (m_minGroupSize != minGroupSize) {
        m_minGroupSize = minGroupSize;
        if (m_maxSize > minGroupSize) {
            m_maxSize = m_minGroupSize;
            updateCulling(m_treeRoot);
        }
        updateGroupRendering(&m_treeRoot);
        update();
    }
}

void TreeMapWidget::zoomIn(void *userData)
{
    if (Node *found = getNodeWithUserData(&m_root, userData)) {
        m_zoomStack << found;

        setSelectedNode(nullptr, QPoint());
        setHoveredNode(nullptr, QPoint());

        m_renderedNode = found;
        relayoutTreeMapping(m_treeRoot, *m_renderedNode, m_viewport);
        updateCulling(m_treeRoot);
        updateGroupRendering(&m_treeRoot);
        update();
    }
}

void TreeMapWidget::zoomOut()
{
    if (!m_zoomStack.isEmpty()) {
        m_zoomStack.removeLast();

        setSelectedNode(nullptr, QPoint());
        setHoveredNode(nullptr, QPoint());

        m_renderedNode = m_zoomStack.empty() ? &m_root : m_zoomStack.last();
        relayoutTreeMapping(m_treeRoot, *m_renderedNode, m_viewport);
        updateCulling(m_treeRoot);
        updateGroupRendering(&m_treeRoot);
        update();
    }
}

void TreeMapWidget::resizeEvent(QResizeEvent *event)
{
    QOpenGLWidget::resizeEvent(event);

    m_viewport = QRectF(0, 0, width(), height());
    relayoutTreeMapping(m_treeRoot, *m_renderedNode, m_viewport);
    updateCulling(m_treeRoot);
    updateGroupRendering(&m_treeRoot);
    update();
}

void TreeMapWidget::initializeGL()
{
    if (!m_shader.addShaderFromSourceCode(QOpenGLShader::Vertex, s_vs)
            || !m_shader.addShaderFromSourceCode(QOpenGLShader::Fragment, s_fs)
            || !m_shader.link()) {
        qWarning() << m_shader.log();
    }

    m_vertexBuffer.create();
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
        const float x2 = rect.right();
        const float y1 = rect.top();
        const float y2 = rect.bottom();
        const quint8 uv0 = 0;
        const quint8 uv1 = 255;

        reserve(6 * stride());
        *this << x1 << y1 << bg << fade << uv0 << uv0 << uv0 << uv0;
        *this << x1 << y2 << bg << fade << uv0 << uv1 << uv0 << uv0;
        *this << x2 << y2 << bg << fade << uv1 << uv1 << uv0 << uv0;

        *this << x2 << y2 << bg << fade << uv1 << uv1 << uv0 << uv0;
        *this << x2 << y1 << bg << fade << uv1 << uv0 << uv0 << uv0;
        *this << x1 << y1 << bg << fade << uv0 << uv0 << uv0 << uv0;
    }

    int vertices() const { return size() / stride(); }
    constexpr static int stride() { return 20; }
};

void TreeMapWidget::paintGL()
{
    QOpenGLContext *gl = QOpenGLContext::currentContext();
    VertexBuffer vertices;

    const auto render = [&](float border) {
        m_vertexBuffer.bind();
        m_vertexBuffer.allocate(vertices.data(), vertices.size());

        m_shader.bind();
        m_shader.setUniformValue("screenSize", QVector2D(width(), height()));
        m_shader.setUniformValue("border", border);
        m_shader.enableAttributeArray("pos");
        m_shader.setAttributeBuffer("pos", GL_FLOAT, 0, 2, VertexBuffer::stride());
        m_shader.enableAttributeArray("bgColor");
        m_shader.setAttributeBuffer("bgColor", GL_UNSIGNED_BYTE, 8, 4, VertexBuffer::stride());
        m_shader.enableAttributeArray("fadeColor");
        m_shader.setAttributeBuffer("fadeColor", GL_UNSIGNED_BYTE, 12, 4, VertexBuffer::stride());
        m_shader.enableAttributeArray("uv");
        m_shader.setAttributeBuffer("uv", GL_UNSIGNED_BYTE, 16, 2, VertexBuffer::stride());

        gl->functions()->glDisable(GL_CULL_FACE);
        gl->functions()->glEnable(GL_BLEND);
        gl->functions()->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gl->functions()->glBlendEquation(GL_FUNC_ADD);
        gl->functions()->glDrawArrays(GL_TRIANGLES, 0, vertices.vertices());

        m_shader.disableAttributeArray("pos");
        m_shader.disableAttributeArray("bgColor");
        m_shader.disableAttributeArray("fadeColor");
        m_shader.disableAttributeArray("uv");

        m_vertexBuffer.release();
        m_shader.release();
    };

    // Render all nodes that are visible in the view
    traverseRenderNodes(*m_renderedNode, [&](const Node &node) {
        if (node.renderState == Render)
            vertices.add(node.viewRect, node.color, QColor(0, 0, 0));
        return (node.renderState == RenderChildren);
    });
    render(0.3f);

    const QColor paintColor(0, 0, 0);

    // Render selected/highlighted outlines and node labels
    QPainter painter(this);
    traverseRenderNodes(*m_renderedNode, [&](const Node &node) {
        if (node.renderState == Render) {
            // draw rect
            painter.setBrush(Qt::NoBrush);
            if (m_selectedNode == &node) {
                painter.setPen(QPen(paintColor, 3.0f));
                painter.drawRect(node.viewRect.adjusted(0, 0, -1.5f, -1.5f));
            }
            else if (m_hoveredNode == &node) {
                painter.setPen(QPen(paintColor, 2.0f));
                painter.drawRect(node.viewRect.adjusted(0, 0, 1.0f, 1.0f));
            }

            // possibly draw label text, if there is enough space
            if (node.viewRect.width() > 10 && node.viewRect.height() > 5) {
                painter.setPen(paintColor);
                const QRectF bounds = painter.boundingRect(node.viewRect, Qt::AlignHCenter | Qt::AlignCenter, node.label);
                if (bounds.width() < node.viewRect.width() + 10 && bounds.height() < node.viewRect.height() + 5) {
                    painter.drawText(node.viewRect, Qt::AlignHCenter | Qt::AlignCenter, node.label);
                }
            }
        }
        return (node.renderState == RenderChildren);
    });

    // render group node outlines (blended on top)
    painter.beginNativePainting();
    vertices.clear();
    traverseRenderNodes(*m_renderedNode, [&](const Node &node) {
        if (!node.groupViewRect.isNull()) {
            vertices.add(node.groupViewRect, QColor(0, 0, 0, 0), QColor(0, 0, 0));
        }
        return node.responsibleForGroup;
    });
    render(0.6f);
    painter.endNativePainting();

    // render group node labels
    painter.setPen(QPen(QColor(255, 255, 255), 1.0f));
    traverseRenderNodes(*m_renderedNode, [&](const Node &node) {
        if (!node.groupViewRect.isNull()) {
            QRectF bounds = node.groupViewRect;
            bounds.adjust(GROUP_LABEL_OFFSET, GROUP_LABEL_OFFSET, -GROUP_LABEL_OFFSET, -GROUP_LABEL_OFFSET);
            bounds = bounds.intersected(QRectF(0, 0, width(), height()));
            painter.drawText(bounds, node.groupLabel);
        }
        return node.responsibleForGroup;
    });
}

void TreeMapWidget::traverseRenderNodes(const TreeMapWidget::Node &node, const TreeMapWidget::NodeTraversalFunctor &visitor)
{
    if (visitor(node)) {
        for (const Node &child : node.children)
            traverseRenderNodes(child, visitor);
    }
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

QRectF TreeMapWidget::sceneToView(const QRectF &rect) const
{
    QRectF projectedRect = rect;
    projectedRect.translate(-m_viewport.left(), -m_viewport.top());
    return scaled(projectedRect, width() / m_viewport.width());
}

QRectF TreeMapWidget::viewToScene(const QRectF &rect) const
{
    QRectF projectedRect = scaled(rect, m_viewport.width() / width());
    return projectedRect.translated(m_viewport.left(), m_viewport.top());
}

QPointF TreeMapWidget::viewToScene(const QPointF &pt) const
{
    const float scale = m_viewport.width() / width();
    return QPointF(m_viewport.left() + pt.x() * scale, m_viewport.top() + pt.y() * scale);
}

void TreeMapWidget::setViewport(QRectF rect)
{
    if (rect.width() > width())
        rect.setWidth(width());
    if (rect.height() > height())
        rect.setHeight(height());
    if (rect.left() < 0.0f)
        rect.moveLeft(0.0f);
    if (rect.top() < 0.0f)
        rect.moveTop(0.0f);
    if (rect.right() > width())
        rect.moveRight(width());
    if (rect.bottom() > height())
        rect.moveBottom(height());

    m_viewport = rect;
    updateCulling(m_treeRoot);
    updateGroupRendering(&m_treeRoot);
    update();
}

const TreeMapWidget::Node *TreeMapWidget::getNodeAt(QPoint pt, const Node *parent) const
{
    if (parent->renderState == Render)
        return parent->viewRect.contains(pt) ? parent : nullptr;

    if (parent->renderState == RenderChildren) {
        if (!parent->groupViewRect.isNull() && parent->groupViewRect.contains(pt)) {
            const QPointF topLeft = parent->groupViewRect.topLeft();
            const QRectF labelRect = parent->groupLabelBounds.translated(topLeft);
            if (labelRect.contains(pt))
                return parent;
        }
        for (const Node &child : parent->children) {
            if (const Node *found = getNodeAt(pt, &child))
                return found;
        }
    }

    return nullptr;
}

TreeMapWidget::Node *TreeMapWidget::getNodeWithUserData(TreeMapWidget::Node *node, void *data) const
{
    if (node && node->userData == data)
        return node;
    for (Node &child : node->children) {
        if (Node *found = getNodeWithUserData(&child, data))
            return found;
    }
    return nullptr;
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
