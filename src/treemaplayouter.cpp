#include "treemaplayouter.h"

#include <memory>
#include <algorithm>

#include <QtMath>
#include <QDebug>

#include <QOpenGLContext>
#include <QOpenGLFunctions>

static constexpr float GROUP_LABEL_OFFSET = 0.5f;

using SquarifyNode = Squarify::TreeMapNode;
using Squarify::Rect;

static QRectF scaled(const QRectF &rect, float scale)
{
    return QRectF(rect.left() * scale, rect.top() * scale, rect.width() * scale, rect.height() * scale);
}

static QRectF RectToQRect(const Rect &r) { return QRectF(r.x, r.y, r.width, r.height); }
static Rect QRectToRect(const QRectF &r) { return Rect(r.left(), r.top(), r.width(), r.height()); }

TreeMapLayouter::TreeMapLayouter(int width, int height)
    : m_width(width)
    , m_height(height)
{
}

TreeMapLayouter::~TreeMapLayouter()
{
}

void TreeMapLayouter::setRootNode(const TreeMapNode &root)
{
    m_root.children.clear();
    m_zoomStack.clear();
    m_viewport = QRectF(0, 0, m_width, m_height);

    rebuildNodeTree(m_root, root, 0);
    m_renderedNode = &m_root;

    relayoutTreeMapping(m_treeRoot, *m_renderedNode, m_viewport);
    updateCulling(m_treeRoot);
    updateGroupRendering(&m_treeRoot);

    onNodeTreeChanged();
    onLayoutChanged();
    onViewportChanged();
}

void TreeMapLayouter::rebuildNodeTree(Node &dstNode, const TreeMapNode &srcNode, int depth)
{
    dstNode.label = srcNode.label;
    dstNode.groupLabel = srcNode.groupLabel;
    dstNode.groupLabelBounds = getTextBounds(dstNode.groupLabel);
    dstNode.groupLabelBounds.translate(-dstNode.groupLabelBounds.topLeft());
    dstNode.color = srcNode.color;
    dstNode.size = srcNode.size;
    dstNode.userData = srcNode.userData;

    dstNode.depth = depth;
    dstNode.children.resize(srcNode.children.size());
    for (int i  = 0; i < dstNode.children.size(); ++i) {
        rebuildNodeTree(dstNode.children[i], srcNode.children[i], depth + 1);
    }
}

void TreeMapLayouter::relayoutTreeMapping(TreeNode &treeNode, Node &node, const QRectF &rect)
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

void TreeMapLayouter::updateCulling(TreeNode &treeNode, bool fullyVisible, bool culledParent)
{
    if (treeNode.node == nullptr)
        return;

    if (culledParent) {
        treeNode.node->renderState = CulledChildren;
        return;
    }

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

    if (tooSmall || tooDeep || tooUnparenty)
        treeNode.node->renderState = Render;
    else
        treeNode.node->renderState = RenderChildren;

    for (TreeNode::Subdivision &sub : treeNode.subdivisions) {
        for (TreeNode &node : sub.subnodes) {
            updateCulling(node, fullyVisible, treeNode.node->renderState == Render);
        }
    }
}

void TreeMapLayouter::updateGroupRendering(TreeNode *treeNode)
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
    const float ratio = m_viewport.width() / m_width;
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

void TreeMapLayouter::setMaxDepth(int maxDepth)
{
    if (m_maxDepth != maxDepth) {
        m_maxDepth = maxDepth;
        updateCulling(m_treeRoot);
        updateGroupRendering(&m_treeRoot);
        onViewportChanged();
    }
}

void TreeMapLayouter::setMaxSize(int maxSize)
{
    maxSize = qMax(maxSize, 1);
    maxSize = qMin(maxSize, m_minGroupSize);
    if (m_maxSize != maxSize) {
        m_maxSize = maxSize;
        updateCulling(m_treeRoot);
        updateGroupRendering(&m_treeRoot);
        onViewportChanged();
    }
}

void TreeMapLayouter::setMinGroupSize(int minGroupSize)
{
    minGroupSize = qMax(minGroupSize, 50);
    if (m_minGroupSize != minGroupSize) {
        m_minGroupSize = minGroupSize;
        if (m_maxSize > minGroupSize) {
            m_maxSize = m_minGroupSize;
            updateCulling(m_treeRoot);
        }
        updateGroupRendering(&m_treeRoot);
        onViewportChanged();
    }
}

void TreeMapLayouter::zoomIn(void *userData)
{
    if (Node *found = getNodeWithUserData(&m_root, userData)) {
        m_zoomStack << found;
        m_renderedNode = found;
        relayoutTreeMapping(m_treeRoot, *m_renderedNode, m_viewport);
        updateCulling(m_treeRoot);
        updateGroupRendering(&m_treeRoot);

        onLayoutChanged();
        onViewportChanged();
    }
}

void TreeMapLayouter::zoomOut()
{
    if (!m_zoomStack.isEmpty()) {
        m_zoomStack.removeLast();
        m_renderedNode = m_zoomStack.empty() ? &m_root : m_zoomStack.last();
        relayoutTreeMapping(m_treeRoot, *m_renderedNode, m_viewport);
        updateCulling(m_treeRoot);
        updateGroupRendering(&m_treeRoot);

        onLayoutChanged();
        onViewportChanged();
    }
}

void TreeMapLayouter::resize(int width, int height)
{
    m_width = width;
    m_height = height;
    m_viewport = QRectF(0, 0, width, height);
    relayoutTreeMapping(m_treeRoot, *m_renderedNode, m_viewport);
    updateCulling(m_treeRoot);
    updateGroupRendering(&m_treeRoot);

    onLayoutChanged();
    onViewportChanged();
}

void TreeMapLayouter::traverseRenderNodes(const TreeMapLayouter::Node &node, const TreeMapLayouter::NodeTraversalFunctor &visitor)
{
    if (visitor(node)) {
        for (const Node &child : node.children)
            traverseRenderNodes(child, visitor);
    }
}

QRectF TreeMapLayouter::sceneToView(const QRectF &rect) const
{
    QRectF projectedRect = rect;
    projectedRect.translate(-m_viewport.left(), -m_viewport.top());
    return scaled(projectedRect, m_width / m_viewport.width());
}

QRectF TreeMapLayouter::viewToScene(const QRectF &rect) const
{
    QRectF projectedRect = scaled(rect, m_viewport.width() / m_width);
    return projectedRect.translated(m_viewport.left(), m_viewport.top());
}

QPointF TreeMapLayouter::viewToScene(const QPointF &pt) const
{
    const float scale = m_viewport.width() / m_width;
    return QPointF(m_viewport.left() + pt.x() * scale, m_viewport.top() + pt.y() * scale);
}

void TreeMapLayouter::setViewport(const QRectF &rect)
{
    m_viewport = rect;

    if (m_viewport.width() > m_width)
        m_viewport.setWidth(m_width);
    if (m_viewport.height() > m_height)
        m_viewport.setHeight(m_height);
    if (m_viewport.left() < 0.0f)
        m_viewport.moveLeft(0.0f);
    if (m_viewport.top() < 0.0f)
        m_viewport.moveTop(0.0f);
    if (m_viewport.right() > m_width)
        m_viewport.moveRight(m_width);
    if (m_viewport.bottom() > m_height)
        m_viewport.moveBottom(m_height);

    updateCulling(m_treeRoot);
    updateGroupRendering(&m_treeRoot);

    onViewportChanged();
}

const TreeMapLayouter::Node *TreeMapLayouter::getNodeAt(QPoint pt, const Node *parent) const
{
    if (parent->renderState == Render)
        return parent->viewRect.contains(pt) ? parent : nullptr;

    if (parent->renderState == RenderChildren) {
        if (!parent->viewRect.contains(pt))
            return nullptr;

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

TreeMapLayouter::Node *TreeMapLayouter::getNodeWithUserData(TreeMapLayouter::Node *node, void *data) const
{
    if (node && node->userData == data)
        return node;
    for (Node &child : node->children) {
        if (Node *found = getNodeWithUserData(&child, data))
            return found;
    }
    return nullptr;
}
